// SPDX-License-Identifier: GPL-3.0-or-later
#include "clang/prune_extract.h"
#include "core/names/names.h"
#include "resources.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroArgs.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace distill {
namespace {
using namespace clang;
// These affect diagnostics, branch/inlining hints or layout, never add symbol
// references. Attribute arguments are still visited and remain in family text.
bool supportedAttribute(const Attr* a) {
  return isa<WarnUnusedResultAttr,UnusedAttr,LikelyAttr,UnlikelyAttr,AlwaysInlineAttr,AlignedAttr,MaxFieldAlignmentAttr>(a);
}
bool scalarBuiltin(const Decl* d) {
  const auto* f = dyn_cast<FunctionDecl>(d);
  if (!f || !f->isImplicit() || !f->getBuiltinID()) return false;
  // Clang gives these implicit declarations the user's call-site location.
  // They operate on scalars/control flow only; arguments are traversed normally.
  // Do not generalize to arbitrary builtins or explicitly declared user code.
  static const std::set<std::string,std::less<>> names = {
    "__builtin_popcount", "__builtin_popcountl", "__builtin_popcountll",
    "__builtin_clz", "__builtin_clzl", "__builtin_clzll",
    "__builtin_ctz", "__builtin_ctzl", "__builtin_ctzll",
    "__builtin_ffs", "__builtin_ffsl", "__builtin_ffsll",
    "__builtin_parity", "__builtin_parityl", "__builtin_parityll",
    "__builtin_bswap16", "__builtin_bswap32", "__builtin_bswap64", "__builtin_bswap128",
    "__builtin_expect", "__builtin_expect_with_probability", "__builtin_unreachable"
  };
  return names.contains(f->getNameAsString());
}
struct Extraction {
  PruningModel model;
  CompilerInstance& compiler;
  SourceManager& sm;
  std::map<const Decl*,FamilyId> owners;
  std::vector<std::pair<const Decl*,FamilyId>> declarations;
  std::map<std::size_t,std::pair<bool,bool>> pasteGroups;
  std::vector<EditRange> macroInvocations, conditionRanges;
  std::vector<distill::Token> tokens;
  std::map<std::size_t,std::set<std::string>> systemMacros;
  explicit Extraction(PruningModel m, CompilerInstance& c) : model(std::move(m)), compiler(c), sm(c.getSourceManager()) {
    auto scan = scanDirectives(model.text);
    if (!scan.ok()) { disable("directive scan failure in flat.cpp: " + scan.error().message); return; }
    for (const auto& l : scan.value().lines) if (!l.directive) tokens.insert(tokens.end(),l.tokens.begin(),l.tokens.end());
    for (const auto& l : scan.value().lines) if (l.directive &&
        (l.directive->name == "if" || l.directive->name == "elif" || l.directive->name == "ifdef" || l.directive->name == "ifndef")) conditionRanges.push_back({l.begin,l.end});
  }
  bool user(clang::SourceLocation loc) const { return loc.isValid() && sm.isWrittenInMainFile(sm.getExpansionLoc(loc)); }
  std::size_t offset(clang::SourceLocation loc) const { return sm.getFileOffset(sm.getExpansionLoc(loc)); }
  void disable(std::string reason) {
    if (std::find(model.disabledReasons.begin(),model.disabledReasons.end(),reason) == model.disabledReasons.end()) model.disabledReasons.push_back(std::move(reason));
  }
  std::optional<EditRange> range(SourceRange source) const {
    if (!user(source.getBegin()) || !user(source.getEnd())) return {};
    auto begin = offset(source.getBegin());
    auto end = Lexer::getLocForEndOfToken(sm.getExpansionLoc(source.getEnd()),0,sm,compiler.getLangOpts());
    if (end.isInvalid()) return {};
    const auto stop = offset(end);
    if (begin >= stop || stop > model.text.size()) return {};
    return EditRange{begin,stop};
  }
  const Decl* canonical(const Decl* d) const { return d ? d->getCanonicalDecl() : nullptr; }
  FamilyId owner(const Decl* declaration) const {
    if (!declaration) return noFamily;
    if (auto i = owners.find(declaration); i != owners.end()) return i->second;
    if (auto i = owners.find(canonical(declaration)); i != owners.end()) return i->second;
    // Pattern, canonical and context links can form cycles (notably injected
    // class names). Try every route without letting a failed pattern lookup
    // hide an enclosing record's owner. Direct bindings preserve specializations.
    std::vector<const Decl*> pending{declaration}; std::set<const Decl*> seen;
    while (!pending.empty()) {
      const auto* d = pending.back(); pending.pop_back();
      if (!d || !seen.insert(d).second) continue;
      if (auto i = owners.find(d); i != owners.end()) return i->second;
      if (auto i = owners.find(canonical(d)); i != owners.end()) return i->second;
      auto context = [&](const DeclContext* ctx) {
        if (ctx && !ctx->isTranslationUnit() && !ctx->isNamespace()) pending.push_back(Decl::castFromDeclContext(ctx));
      };
      context(d->getLexicalDeclContext());
      context(d->getDeclContext()); // Out-of-line members have namespace lexical context.
      if (const auto* f = dyn_cast<FunctionDecl>(d)) {
        if (f->getTemplateSpecializationKind() != TSK_ExplicitSpecialization) {
          pending.push_back(f->getPrimaryTemplate());
          pending.push_back(f->getInstantiatedFromDecl());
          pending.push_back(f->getInstantiatedFromMemberFunction());
          pending.push_back(f->getTemplateInstantiationPattern());
        }
        if (const auto* guide = dyn_cast<CXXDeductionGuideDecl>(f)) pending.push_back(guide->getDeducedTemplate());
      }
      if (const auto* r = dyn_cast<CXXRecordDecl>(d)) {
        pending.push_back(r->getInstantiatedFromMemberClass());
        if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(r)) {
          if (spec->getSpecializationKind() != TSK_ExplicitSpecialization) {
            pending.push_back(spec->getSpecializedTemplate());
            pending.push_back(r->getTemplateInstantiationPattern()); // Includes partial specializations.
          }
        }
        if (r->isLambda()) pending.push_back(r->getLambdaContextDecl());
      }
      if (const auto* v = dyn_cast<VarDecl>(d); v && v->getTemplateSpecializationKind() != TSK_ExplicitSpecialization) {
        pending.push_back(v->getInstantiatedFromStaticDataMember());
        pending.push_back(v->getTemplateInstantiationPattern());
        if (const auto* spec = dyn_cast<VarTemplateSpecializationDecl>(v)) pending.push_back(spec->getSpecializedTemplate());
      }
      if (const auto* en = dyn_cast<EnumDecl>(d)) pending.push_back(en->getInstantiatedFromMemberEnum());
      if (const auto* t = dyn_cast<RedeclarableTemplateDecl>(d)) pending.push_back(t->getInstantiatedFromMemberTemplate());
    }
    return noFamily;
  }
  void bindParameters(const TemplateParameterList* parameters, FamilyId id) {
    if (parameters) for (const auto* p : *parameters) bind(p,id);
  }
  void bind(const Decl* d, FamilyId id) {
    owners[d] = id;
    auto [it,fresh] = owners.emplace(canonical(d),id);
    if (!fresh && it->second != id) model.unions.emplace_back(id,it->second);
    // Template parameters are not DeclContext children, and often have a TU
    // semantic context. Out-of-line definitions own separate outer parameters.
    if (const auto* t = dyn_cast<TemplateDecl>(d)) {
      bindParameters(t->getTemplateParameters(),id);
      if (t->getTemplatedDecl()) bind(t->getTemplatedDecl(),id);
    }
    // Prototype-scope parameters (including nested function declarators) need
    // not appear in DeclContext::decls(). Bind them before dependency traversal.
    if (const auto* f = dyn_cast<FunctionDecl>(d)) for (const auto* p : f->parameters()) bind(p,id);
    if (const auto* v = dyn_cast<DeclaratorDecl>(d)) for (const auto* p : v->getTemplateParameterLists()) bindParameters(p,id);
    if (const auto* t = dyn_cast<TagDecl>(d)) for (const auto* p : t->getTemplateParameterLists()) bindParameters(p,id);
    if (const auto* t = dyn_cast<ClassTemplatePartialSpecializationDecl>(d)) bindParameters(t->getTemplateParameters(),id);
    if (const auto* t = dyn_cast<VarTemplatePartialSpecializationDecl>(d)) bindParameters(t->getTemplateParameters(),id);
    if (const auto* ctx = dyn_cast<DeclContext>(d)) for (auto* child : ctx->decls()) if (!isa<NamespaceDecl>(child)) bind(child,id);
  }
  std::string describe(const Decl* d) const {
    std::string text = d->getDeclKindName();
    if (const auto* n = dyn_cast<NamedDecl>(d)) text += " '" + n->getQualifiedNameAsString() + "'";
    auto loc = d->getLocation().isValid() ? d->getLocation() : d->getBeginLoc();
    text += " at " + loc.printToString(sm);
    if (loc.isMacroID()) text += " (spelling " + sm.getSpellingLoc(loc).printToString(sm) + ")";
    return text;
  }
  void index(const NamedDecl* d, PruneFamily& f, bool callable = false) {
    if (!d || d->getDeclName().isEmpty()) return;
    auto name = d->getNameAsString();
    if (d->getDeclName().getNameKind() == DeclarationName::CXXLiteralOperatorName)
      name = d->getDeclName().getCXXLiteralIdentifier()->getName().str();
    f.names.insert(name);
    if (callable) f.callableNames.insert(name);
  }
  void discover(Decl* d) {
    if (!d || d->isImplicit() || !user(d->getBeginLoc())) return;
    if (auto* ns = dyn_cast<NamespaceDecl>(d)) { for (auto* child : ns->decls()) discover(child); return; }
    if (auto* link = dyn_cast<LinkageSpecDecl>(d)) {
      const auto first = model.families.size();
      for (auto* child : link->decls()) discover(child);
      if (!link->hasBraces()) for (auto id = first; id < model.families.size(); ++id)
        for (auto& r : model.families[id].ranges) r.begin = std::min(r.begin,offset(link->getBeginLoc()));
      return;
    }
    const auto id = model.families.size(); PruneFamily f;
    const Decl* base = d;
    // Concepts are TemplateDecls with no templated declaration. Keep the
    // ConceptDecl itself as the family rather than dereferencing nullptr.
    if (const auto* t = dyn_cast<TemplateDecl>(d); t && t->getTemplatedDecl()) base = t->getTemplatedDecl();
    if (const auto* n = dyn_cast<NamedDecl>(base)) {
      f.label = n->getQualifiedNameAsString();
      if (!n->getDeclContext()->isRecord()) index(n,f,isa<FunctionDecl>(base));
    }
    if (f.label.empty()) f.label = std::string(d->getDeclKindName()) + "@" + std::to_string(offset(d->getBeginLoc()));
    if (isa<RecordDecl>(base)) f.kind = FamilyKind::record;
    else if (const auto* en = dyn_cast<EnumDecl>(base)) {
      f.kind = FamilyKind::enumeration;
      if (!en->isScoped()) for (auto* item : en->enumerators()) index(item,f);
    } else if (const auto* fn = dyn_cast<FunctionDecl>(base)) {
      f.kind = FamilyKind::function; f.nonMemberOperator = (fn->isOverloadedOperator() || fn->getDeclName().getNameKind() == DeclarationName::CXXLiteralOperatorName) && !isa<CXXMethodDecl>(fn);
      f.externCDefinition = fn->isExternC() && fn->isThisDeclarationADefinition();
      if (fn->getTemplateSpecializationKind() == TSK_ExplicitInstantiationDeclaration || fn->getTemplateSpecializationKind() == TSK_ExplicitInstantiationDefinition) f.root = "explicit instantiation";
    } else if (const auto* v = dyn_cast<VarDecl>(base)) {
      f.kind = FamilyKind::variable; f.externCDefinition = v->isExternC() && v->isThisDeclarationADefinition() != VarDecl::DeclarationOnly;
    } else if (isa<TypedefNameDecl>(base) || isa<NamespaceAliasDecl>(base)) f.kind = FamilyKind::alias;
    else if (isa<ConceptDecl>(base)) f.kind = FamilyKind::conceptDecl;
    else if (isa<UsingDecl>(base) || isa<UsingDirectiveDecl>(base) || isa<UsingEnumDecl>(base)) { f.kind = FamilyKind::usingDecl; f.namespaceUsing = true; }
    else if (isa<StaticAssertDecl>(base)) { f.kind = FamilyKind::assertion; f.root = "namespace static_assert"; }
    else if (isa<EmptyDecl>(base)) { /* Complete, inert declaration. */ }
    else if (isa<FileScopeAsmDecl>(base)) f.root = "file-scope asm";
    else f.pinned = std::string("unsupported declaration: ") + base->getDeclKindName();
    if (const auto* s = dyn_cast<ClassTemplateSpecializationDecl>(base)) {
      auto* primary = s->getSpecializedTemplate();
      if (s->getSpecializationKind() == TSK_ExplicitInstantiationDeclaration || s->getSpecializationKind() == TSK_ExplicitInstantiationDefinition) f.root = "explicit instantiation";
      if (!user(primary->getBeginLoc())) { f.kind = FamilyKind::specialization; f.systemSpecialization = true; }
    }
    if (const auto* s = dyn_cast<VarTemplateSpecializationDecl>(base)) {
      if (!user(s->getSpecializedTemplate()->getBeginLoc())) { f.kind = FamilyKind::specialization; f.systemSpecialization = true; }
    }
    if (const auto* fn = dyn_cast<FunctionDecl>(base)) if (auto* t = fn->getPrimaryTemplate())
      if (!user(t->getBeginLoc())) { f.kind = FamilyKind::specialization; f.systemSpecialization = true; }
    auto r = range(d->getSourceRange());
    if (!r) { disable("unbounded declaration range: " + f.label); return; }
    // A member FunctionTemplateDecl starts at its own template header, but
    // its templated declarator also owns the enclosing class template headers
    // in an out-of-line definition. Neither range alone covers every template
    // spelling (ordinary templates need the wrapper's header).
    if (base != d) {
      auto declarator = range(base->getSourceRange());
      if (!declarator) { disable("unbounded templated declaration range: " + f.label); return; }
      r->begin = std::min(r->begin,declarator->begin);
      r->end = std::max(r->end,declarator->end);
    }
    if (d->getBeginLoc().isMacroID() || d->getEndLoc().isMacroID() ||
        (isa<NamedDecl>(base) && cast<NamedDecl>(base)->getLocation().isMacroID())) f.pinned = "macro invocation";
    // A complete declaration includes its terminator and any shared declarators.
    // Functions with bodies end at }, other declarations run through their ;.
    bool body = false;
    if (const auto* fn = dyn_cast<FunctionDecl>(base)) body = fn->doesThisDeclarationHaveABody();
    auto next = std::lower_bound(tokens.begin(),tokens.end(),r->end,[](const auto& token,auto position) { return token.begin < position; });
    bool finished = body;
    int parens = 0, braces = 0, brackets = 0;
    for (; !finished && next != tokens.end(); ++next) {
      const auto& t = *next;
      if (t.text == ";" && parens == 0 && braces == 0 && brackets == 0) { r->end = t.end; finished = true; break; }
      if (t.text == "}") { if (braces == 0) break; --braces; }
      else if (t.text == "{") ++braces;
      else if (t.text == "(") ++parens;
      else if (t.text == ")") { if (parens == 0) break; --parens; }
      else if (t.text == "[") ++brackets;
      else if (t.text == "]") { if (brackets == 0) break; --brackets; }
    }
    if (!finished) { f.pinned = "unrecognized declaration terminator"; }
    // Clang excludes leading [[...]] and alignas(...) from some Decl ranges.
    // Own their complete spelling too: leaving an orphan attribute could attach
    // it to the next declaration after deletion. GNU attributes are normally
    // already included. A macro prefix stays pinned as one complete invocation.
    for (const auto* attr : base->attrs()) if (!attr->isInherited() && !attr->isImplicit() &&
        user(attr->getRange().getBegin()) && offset(attr->getRange().getBegin()) < r->begin) {
      const auto loc = attr->getRange().getBegin();
      auto begin = offset(loc);
      if (loc.isMacroID()) f.pinned = "macro invocation";
      else {
        auto token = std::lower_bound(tokens.begin(),tokens.end(),begin,[](const auto& t,auto p) { return t.begin < p; });
        if (token == tokens.end() || token->begin != begin) { disable("unbounded leading attribute: " + describe(d)); continue; }
        if (token->text != "alignas" && token->text != "__attribute__") {
          // Attr ranges start at the name/scope inside the [[ delimiter.
          while (token != tokens.begin() && token->text != ";" && token->text != "{" && token->text != "}") {
            --token;
            if (token->text == "[" && token != tokens.begin() && std::prev(token)->text == "[") { --token; break; }
          }
          if (token->text != "[") { disable("unbounded leading attribute: " + describe(d)); continue; }
        }
        begin = token->begin;
      }
      r->begin = std::min(r->begin,begin);
    }
    f.ranges.push_back(*r);
    const auto physical = sm.getLineNumber(sm.getMainFileID(),r->begin);
    if (physical && physical <= model.lineMap.size()) f.original = model.lineMap[physical-1];
    model.families.push_back(std::move(f)); declarations.emplace_back(d,id); bind(d,id);
    // An out-of-line member specialization can have a distinct canonical Decl
    // and no primary function template. Whole-record retention still includes
    // it, even when only the judge instantiates that specialization's class.
    if (const auto* record = dyn_cast<CXXRecordDecl>(d->getDeclContext())) {
      const auto enclosing = owner(record);
      if (enclosing != noFamily && enclosing != id) model.unions.emplace_back(id,enclosing);
    }
  }
  void normalizeRanges() {
    // Shared declarators are one deletion unit. Union the retention obligations,
    // then partition their byte ownership; never coalesce physical edit ranges.
    struct R { EditRange range; FamilyId id; }; std::vector<R> all;
    for (FamilyId id = 0; id < model.families.size(); ++id) for (auto r : model.families[id].ranges) all.push_back({r,id});
    std::sort(all.begin(),all.end(),[](auto a,auto b) { return a.range.begin < b.range.begin; });
    std::size_t end = 0; FamilyId prior = noFamily;
    for (auto r : all) {
      auto& f = model.families[r.id]; f.ranges.clear();
      if (r.range.begin < end) model.unions.emplace_back(prior,r.id);
      if (r.range.end > end) f.ranges.push_back({std::max(end,r.range.begin),r.range.end});
      if (r.range.end >= end) { end = r.range.end; prior = r.id; }
    }
  }
};

class Macros final : public PPCallbacks {
  Extraction& e;
  std::map<std::string,MacroFact,std::less<>> sourceBindings;
  MacroFact fact(const MacroInfo* mi) {
    if (!mi || mi->isBuiltinMacro()) return {MacroState::definedUnknown,{}, {}};
    Macro m; m.functionLike = mi->isFunctionLike(); m.variadic = mi->isVariadic();
    for (auto* p : mi->params()) m.parameters.push_back(p->getName().str());
    for (const auto& t : mi->tokens()) { if (!m.replacement.empty()) m.replacement += ' '; m.replacement += e.compiler.getPreprocessor().getSpelling(t); }
    return {MacroState::known,std::move(m),{}};
  }
public:
  explicit Macros(Extraction& x) : e(x) {}
  void PragmaDirective(clang::SourceLocation loc, PragmaIntroducerKind kind) override {
    // The core audits literal #pragma directives. An operator-generated pragma
    // has no such source directive/range, so it needs a separate future audit.
    if (e.user(loc) && kind != PIK_HashPragma) e.disable("unsupported generated user pragma at " + loc.printToString(e.sm));
  }
  void finalToken(const clang::Token& token) {
    if (!token.is(tok::identifier) || !e.user(token.getLocation())) return;
    const auto name = token.getIdentifierInfo()->getName().str();
    if (sourceBindings.contains(name)) return;
    auto it = e.model.judgeInitial.find(name);
    if (it == e.model.judgeInitial.end() || it->second.state == MacroState::undefined) return;
    const auto& f = it->second;
    const bool predefined = std::any_of(f.provenance.begin(),f.provenance.end(),[](auto o) {
      return o == FactOrigin::compilerBuiltin || o == FactOrigin::specialBuiltin || o == FactOrigin::nativeModel || o == FactOrigin::commandOperation;
    });
    if (predefined) e.model.expansions.push_back({e.offset(token.getLocation()),name,true,false,true,
        {MacroState::undefined,{},{}},f,false,false,true});
  }
  void MacroDefined(const clang::Token& name, const MacroDirective* md) override {
    if (e.user(name.getLocation())) sourceBindings[name.getIdentifierInfo()->getName().str()] = fact(md->getMacroInfo());
  }
  void MacroUndefined(const clang::Token& name, const MacroDefinition&, const MacroDirective*) override {
    if (e.user(name.getLocation())) sourceBindings[name.getIdentifierInfo()->getName().str()] = {MacroState::undefined,{}, {}};
  }
  void MacroExpands(const clang::Token& token, const MacroDefinition& definition, SourceRange range, const MacroArgs*) override {
    if (!e.user(range.getBegin())) return;
    const auto* mi = definition.getMacroInfo(); if (!mi) { e.disable("missing macro expansion definition at " + range.getBegin().printToString(e.sm)); return; }
    const auto offset = e.offset(range.getBegin()); const auto name = token.getIdentifierInfo()->getName().str();
    bool condition = false;
    for (auto d : e.conditionRanges) if (d.begin <= offset && offset < d.end) condition = true;
    if (condition) return; // All user conditions have already been resolved or disable pruning.
    const auto def = e.sm.getSpellingLoc(mi->getDefinitionLoc());
    const bool source = def.isValid() && e.sm.isWrittenInMainFile(def);
    // The predefines buffer is marked as system code too. Classify it first:
    // a pasted/rescanned __GNUC__ is a predefined expansion even without any
    // judge snapshot, and must participate in the cross-configuration check.
    const bool command = def.isValid() && e.sm.isWrittenInCommandLineFile(def);
    const bool builtin = !source && (mi->isBuiltinMacro() || !def.isValid() ||
        e.sm.isWrittenInBuiltinFile(def) || command || !e.sm.isInSystemHeader(def));
    const bool system = !source && !builtin && def.isValid() && e.sm.isInSystemHeader(def);
    const bool judgePredefined = e.model.judgeInitial.contains(name) &&
        std::any_of(e.model.judgeInitial.at(name).provenance.begin(),e.model.judgeInitial.at(name).provenance.end(),[](auto o) { return o == FactOrigin::compilerBuiltin || o == FactOrigin::specialBuiltin || o == FactOrigin::nativeModel; });
    MacroFact judge;
    if (auto i = sourceBindings.find(name); i != sourceBindings.end()) judge = i->second;
    else if (auto i = e.model.judgeInitial.find(name); i != e.model.judgeInitial.end()) judge = i->second;
    else if (e.model.judgeProfile && (e.model.judgeComplete || !isReserved(name))) judge.state = MacroState::undefined;
    bool paste = source && std::any_of(mi->tokens_begin(),mi->tokens_end(),[](const auto& t) { return t.is(tok::hashhash); });
    auto parse = fact(mi);
    parse.provenance = {source ? FactOrigin::source : system ? FactOrigin::systemHeader : command ? FactOrigin::commandOperation : FactOrigin::compilerBuiltin};
    e.model.expansions.push_back({offset,name,true,false,builtin || judgePredefined,parse,judge,paste,false,true});
    auto& group = e.pasteGroups[offset]; group.first |= paste; group.second |= system || builtin || judgePredefined;
    if (system) e.systemMacros[offset].insert(name);
    if (auto r = e.range(range)) e.macroInvocations.push_back(*r);
  }
};

// Some local declarations have a TU/prototype semantic context, or aren't
// DeclContext children. Bind lexical ownership before any dependency traversal.
class Ownership final : public RecursiveASTVisitor<Ownership> {
  Extraction& e;
  FamilyId current = noFamily;
  std::size_t visits = 0;
public:
  explicit Ownership(Extraction& x) : e(x) {}
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
  bool TraverseDecl(Decl* d) {
    if (!d) return true;
    if (++visits > 2000000) { e.disable("AST traversal limit: " + e.describe(d)); return false; }
    const auto saved = current;
    if (const auto id = e.owner(d); id != noFamily) current = id;
    const bool result = RecursiveASTVisitor::TraverseDecl(d); current = saved; return result;
  }
  bool VisitDecl(Decl* d) {
    if (current != noFamily && e.user(d->getBeginLoc()) && e.owner(d) == noFamily) e.bind(d,current);
    return true;
  }
  bool VisitRequiresExpr(RequiresExpr* x) {
    if (current != noFamily) for (const auto* p : x->getLocalParameters()) e.bind(p,current);
    return true;
  }
  bool VisitLambdaExpr(LambdaExpr* x) {
    if (current != noFamily && e.owner(x->getLambdaClass()) == noFamily) e.bind(x->getLambdaClass(),current);
    if (current != noFamily) for (const auto& capture : x->captures())
      if (capture.capturesVariable() && capture.getCapturedVar()->isInitCapture()) e.bind(capture.getCapturedVar(),current);
    return true;
  }
};

class Dependencies final : public RecursiveASTVisitor<Dependencies> {
  Extraction& e;
  FamilyId current = noFamily;
  std::size_t systemCurrent = noFamily;
  std::map<const Decl*,std::size_t> systems;
  std::vector<const Decl*> systemPending;
  std::size_t visits = 0;
  std::map<std::pair<const DeclContext*,std::string>,FamilyId> overloads;
  std::map<std::string,std::set<FamilyId>,std::less<>> symbols;
  bool symbolRoot(std::string_view symbol, std::string_view form) {
    auto found = symbols.find(symbol);
    if (found == symbols.end()) {
      e.disable("unresolved " + std::string(form) + " symbol: " + std::string(symbol)); return false;
    }
    for (auto id : found->second) e.model.families[id].root = std::string(form) + " symbol reference";
    return true;
  }
  void assembly(std::string_view text, clang::SourceLocation at) {
    // Only direct symbol calls/branches and address directives are interpreted.
    // Arbitrary assembly is never guessed from identifier-looking substrings.
    std::string lines(text); std::replace(lines.begin(),lines.end(),';','\n');
    std::istringstream input(lines); std::string line;
    while (std::getline(input,line)) {
      std::istringstream words(line); std::string op, symbol, extra;
      if (!(words >> op)) continue;
      if ((op != "call" && op != "callq" && op != "jmp" && op != "bl" && op != "b" &&
           op != ".quad" && op != ".long" && op != ".8byte" && op != ".4byte") ||
          !(words >> symbol) || (words >> extra) ||
          symbol.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.$") != symbol.npos) {
        e.disable("unsupported asm at " + at.printToString(e.sm)); return;
      }
      symbolRoot(symbol,"asm");
    }
  }
  void edge(const Decl* d) {
    if (!d) return;
    // Builtin templates have no source declaration to retain. Their template
    // arguments/result types are still traversed by type()/argument().
    if (isa<BuiltinTemplateDecl>(d)) return;
    if (const auto* shadow = dyn_cast<UsingShadowDecl>(d)) edge(shadow->getTargetDecl());
    const auto target = e.owner(d);
    if (target != noFamily) {
      if (current != noFamily) e.model.families[current].edges.push_back(target);
      else if (systemCurrent != noFamily) e.model.systemEntities[systemCurrent].userEdges.push_back(target);
      return;
    }
    if (scalarBuiltin(d)) return;
    if (e.user(d->getBeginLoc())) { e.disable("unowned user dependency: " + e.describe(d)); return; }
    if (!isa<FunctionDecl>(d) && !isa<CXXRecordDecl>(d) && !isa<VarDecl>(d)) return;
    const auto [it,fresh] = systems.emplace(d,systems.size());
    if (fresh) { e.model.systemEntities.push_back({d->getDeclKindName(),{}, {}}); systemPending.push_back(d); }
    if (current != noFamily) e.model.systemUses.push_back({current,it->second});
    else if (systemCurrent != noFamily) e.model.systemEntities[systemCurrent].systemEdges.push_back(it->second);
  }
  void type(QualType q, std::vector<FamilyId>* associated = nullptr, unsigned depth = 0) {
    if (q.isNull()) return;
    if (depth > 100) { e.disable("type traversal limit for " + q.getAsString()); return; }
    const Type* t = q.getTypePtr();
    if (auto* r = t->getAs<RecordType>()) {
      if (associated) { auto id = e.owner(r->getDecl()); if (id != noFamily) associated->push_back(id); }
      edge(r->getDecl());
      if (const auto* s = dyn_cast<ClassTemplateSpecializationDecl>(r->getDecl())) for (const auto& a : s->getTemplateArgs().asArray()) argument(a,associated,depth+1);
    } else if (auto* en = t->getAs<EnumType>()) { edge(en->getDecl()); if (associated) { auto id = e.owner(en->getDecl()); if (id != noFamily) associated->push_back(id); } }
    if (const auto* a = dyn_cast<TypedefType>(t)) edge(a->getDecl());
    if (const auto* p = dyn_cast<PointerType>(t)) type(p->getPointeeType(),associated,depth+1);
    else if (const auto* p = dyn_cast<ReferenceType>(t)) type(p->getPointeeType(),associated,depth+1);
    else if (const auto* a = dyn_cast<ArrayType>(t)) type(a->getElementType(),associated,depth+1);
    else if (const auto* a = dyn_cast<TemplateSpecializationType>(t)) {
      edge(a->getTemplateName().getAsTemplateDecl());
      for (const auto& arg : a->template_arguments()) argument(arg,associated,depth+1);
    }
  }
  void argument(const TemplateArgument& a, std::vector<FamilyId>* associated, unsigned depth) {
    if (a.getKind() == TemplateArgument::Type) type(a.getAsType(),associated,depth+1);
    else if (a.getKind() == TemplateArgument::Declaration) edge(a.getAsDecl());
    else if (a.getKind() == TemplateArgument::Template) edge(a.getAsTemplate().getAsTemplateDecl());
    else if (a.getKind() == TemplateArgument::Pack) for (const auto& v : a.pack_elements()) argument(v,associated,depth+1);
  }
  bool inertType(QualType q, unsigned depth = 0) {
    if (depth > 50 || q.isNull() || q.isVolatileQualified()) return false;
    const auto* t = q.getTypePtr();
    if (const auto* p = dyn_cast<PointerType>(t)) return inertType(p->getPointeeType(),depth+1);
    if (const auto* a = dyn_cast<ArrayType>(t)) return inertType(a->getElementType(),depth+1);
    if (const auto* a = dyn_cast<TypedefType>(t)) {
      const auto* d = a->getDecl();
      const auto name = d->getQualifiedNameAsString();
      if (!e.user(d->getBeginLoc())) {
        return q->isScalarType() && (name == "std::size_t" || name == "std::ptrdiff_t" ||
          (name.starts_with("std::") && (name.substr(5).starts_with("int") || name.substr(5).starts_with("uint")) && name.ends_with("_t")));
      }
      auto underlying = d->getUnderlyingType();
      const auto* info = d->getTypeSourceInfo();
      if (!info) return false;
      const auto loc = info->getTypeLoc();
      const auto* written = loc.getType().getTypePtr();
      if (!isa<BuiltinType>(written) && !isa<TypedefType>(written)) return false;
      return inertType(underlying,depth+1);
    }
    // Syntactic types only: no deduced auto, decltype, template or member types.
    return isa<BuiltinType>(t);
  }
  bool safeSystemMacros(const VarDecl* v) {
    auto r = e.range(v->getSourceRange()); if (!r) return false;
    for (const auto& [offset,names] : e.systemMacros) if (r->begin <= offset && offset < r->end) for (const auto& name : names) {
      bool allowed = name == "EOF";
      // Positive standard numeric-limit families; internal or other header
      // macros are rejected even when their observed expansion is a literal.
      for (std::string_view prefix : {"CHAR_","SCHAR_","UCHAR_","SHRT_","USHRT_","INT_","UINT_","LONG_","ULONG_","LLONG_","ULLONG_","FLT_","DBL_","LDBL_","WCHAR_","WINT_","SIG_ATOMIC_","SIZE_","PTRDIFF_"})
        allowed |= name.starts_with(prefix) && isStandardMacroName(name);
      allowed |= (name.starts_with("INT") || name.starts_with("UINT")) && isStandardMacroName(name) &&
        (name.ends_with("_MIN") || name.ends_with("_MAX") || name.ends_with("_C"));
      if (!allowed) return false;
    }
    return true;
  }
  std::set<const VarDecl*> inertActive;
  bool inertExpr(const Expr* expr, unsigned depth = 0) {
    if (!expr) return true;
    if (depth > 100 || expr->isValueDependent()) return false;
    expr = expr->IgnoreParens();
    if (isa<IntegerLiteral>(expr) || isa<FloatingLiteral>(expr) || isa<CharacterLiteral>(expr) || isa<CXXBoolLiteralExpr>(expr) || isa<CXXNullPtrLiteralExpr>(expr) || isa<StringLiteral>(expr) || isa<ImplicitValueInitExpr>(expr)) return true;
    if (const auto* ref = dyn_cast<DeclRefExpr>(expr)) {
      if (isa<EnumConstantDecl>(ref->getDecl())) return true;
      if (const auto* v = dyn_cast<VarDecl>(ref->getDecl())) {
        if (!v->getType().isConstQualified() || v->getType().isVolatileQualified() || !inertActive.insert(v).second) return false;
        const bool result = inertType(v->getType()) && safeSystemMacros(v) && inertExpr(v->getInit(),depth+1); inertActive.erase(v); return result;
      }
      return false;
    }
    if (const auto* cast = dyn_cast<CastExpr>(expr)) {
      if (isa<CXXReinterpretCastExpr>(cast) || cast->getCastKind() == CK_FloatingToIntegral ||
          cast->getCastKind() == CK_IntegralToFloating || cast->getCastKind() == CK_FloatingCast) return false;
      if (isa<UserDefinedLiteral>(cast->getSubExpr()) || cast->getCastKind() == CK_UserDefinedConversion || cast->getCastKind() == CK_ConstructorConversion) return false;
      return inertExpr(cast->getSubExpr(),depth+1);
    }
    if (const auto* u = dyn_cast<UnaryOperator>(expr)) return !u->isIncrementDecrementOp() && u->getOpcode() != UO_AddrOf && u->getOpcode() != UO_Deref && inertExpr(u->getSubExpr(),depth+1);
    if (const auto* b = dyn_cast<BinaryOperator>(expr)) return !b->getLHS()->getType()->isFloatingType() && !b->getRHS()->getType()->isFloatingType() && !b->isAssignmentOp() && b->getOpcode() != BO_Comma && inertExpr(b->getLHS(),depth+1) && inertExpr(b->getRHS(),depth+1);
    if (const auto* c = dyn_cast<ConditionalOperator>(expr)) return inertExpr(c->getCond(),depth+1) && inertExpr(c->getTrueExpr(),depth+1) && inertExpr(c->getFalseExpr(),depth+1);
    if (const auto* list = dyn_cast<InitListExpr>(expr)) { for (auto* init : list->inits()) if (!inertExpr(init,depth+1)) return false; return true; }
    if (const auto* trait = dyn_cast<UnaryExprOrTypeTraitExpr>(expr)) {
      if (trait->getTypeOfArgument()->isVariablyModifiedType()) return false;
      return trait->isArgumentType() || inertExpr(trait->getArgumentExpr(),depth+1);
    }
    return false;
  }
public:
  explicit Dependencies(Extraction& x) : e(x) {}
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
  bool TraverseDecl(Decl* d) {
    if (!d) return true;
    if (++visits > 2000000) { e.disable("AST traversal limit: " + e.describe(d)); return false; }
    const auto saved = current;
    if (const auto id = e.owner(d); id != noFamily) current = id;
    const bool result = RecursiveASTVisitor::TraverseDecl(d); current = saved; return result;
  }
  bool VisitDecl(Decl* d) {
    if (d->isInvalidDecl()) e.disable("error-recovery AST: " + e.describe(d));
    if (current != noFamily && e.user(d->getBeginLoc())) for (const auto* attr : d->attrs()) {
      if (isa<ConstructorAttr,DestructorAttr,UsedAttr,SectionAttr,WeakAttr,VisibilityAttr,AsmLabelAttr,AliasAttr>(attr)) {
        e.model.families[current].root = std::string("attribute: ") + attr->getSpelling();
        if (const auto* alias = dyn_cast<AliasAttr>(attr)) symbolRoot(alias->getAliasee().str(),"alias");
      } else if (!supportedAttribute(attr)) e.disable(std::string("unsupported attribute: ") + attr->getSpelling() + " at " + attr->getLocation().printToString(e.sm));
    }
    if (const auto* a = dyn_cast<FileScopeAsmDecl>(d); a && e.user(d->getBeginLoc())) assembly(a->getAsmString(),d->getBeginLoc());
    return true;
  }
  bool VisitAttributedStmt(AttributedStmt* s) {
    if (current != noFamily && e.user(s->getBeginLoc())) for (const auto* attr : s->getAttrs())
      if (!supportedAttribute(attr)) e.disable(std::string("unsupported attribute: ") + attr->getSpelling() + " at " + attr->getLocation().printToString(e.sm));
    return true;
  }
  bool VisitStmt(Stmt* s) {
    if (current == noFamily || !e.user(s->getBeginLoc())) return true;
    if (const auto* a = dyn_cast<GCCAsmStmt>(s)) {
      assembly(a->getAsmString(),s->getBeginLoc());
      // Operand expressions are traversed normally; keeping the enclosing
      // family makes their referenced declarations roots through AST closure.
      e.model.families[current].root = "asm operands";
    } else if (isa<AsmStmt>(s)) e.disable("unsupported asm at " + s->getBeginLoc().printToString(e.sm));
    if (isa<RecoveryExpr>(s)) e.disable("error-recovery AST at " + s->getBeginLoc().printToString(e.sm));
    if (isa<AddrLabelExpr>(s) || isa<IndirectGotoStmt>(s) || isa<StmtExpr>(s) || isa<VAArgExpr>(s))
      e.disable("unsupported va_arg, GNU statement expression or computed goto at " + s->getBeginLoc().printToString(e.sm));
    if (isa<CoroutineBodyStmt>(s) || isa<CoawaitExpr>(s) || isa<CoyieldExpr>(s)) e.disable("unsupported coroutine machinery at " + s->getBeginLoc().printToString(e.sm));
    return true;
  }
  bool VisitExpr(Expr* x) { type(x->getType()); return true; }
  bool VisitTypeLoc(TypeLoc t) { type(t.getType()); return true; }
  bool VisitDeclRefExpr(DeclRefExpr* x) { edge(x->getDecl()); return true; }
  bool VisitUsingShadowDecl(UsingShadowDecl* d) { edge(d->getTargetDecl()); return true; }
  bool VisitMemberExpr(MemberExpr* x) { edge(x->getMemberDecl()); return true; }
  bool VisitCXXConstructExpr(CXXConstructExpr* x) { edge(x->getConstructor()); return true; }
  bool VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr* x) { edge(x->getTemporary()->getDestructor()); return true; }
  bool VisitCXXDeleteExpr(CXXDeleteExpr* x) { edge(x->getOperatorDelete()); type(x->getDestroyedType()); return true; }
  bool VisitCXXNewExpr(CXXNewExpr* x) { edge(x->getOperatorNew()); edge(x->getOperatorDelete()); return true; }
  bool VisitConceptSpecializationExpr(ConceptSpecializationExpr* x) { edge(x->getNamedConcept()); return true; }
  bool VisitUnresolvedLookupExpr(UnresolvedLookupExpr* x) {
    if (current != noFamily && !x->getQualifier()) e.model.families[current].dependentNames.insert(x->getName().getAsString());
    for (auto* d : x->decls()) edge(d);
    return true;
  }
  // Dependent member/qualified lookup needs no global name fallback: records
  // retain all members (including templates, conversions and operator->), and
  // lexical/AST edges retain bases, nested-name carriers and template arguments.
  // System traits select among those carriers; user specializations follow the
  // association rule. This also covers user-pasted member names, since members
  // are never individually pruned. See core/README.md for the closure argument.
  bool VisitFunctionDecl(FunctionDecl* f) {
    if (current == noFamily) return true;
    auto& family = e.model.families[current];
    if (!isa<CXXMethodDecl, CXXDeductionGuideDecl>(f)) {
      const auto key = std::pair{f->getDeclContext()->getPrimaryContext(),f->getNameAsString()};
      auto [it,fresh] = overloads.emplace(key,current); if (!fresh && it->second != current) e.model.unions.emplace_back(current,it->second);
      e.index(f,family,true); family.nonMemberOperator |= f->isOverloadedOperator() || f->getDeclName().getNameKind() == DeclarationName::CXXLiteralOperatorName;
      type(f->getReturnType(),&family.associatedTypes);
      for (auto* p : f->parameters()) type(p->getType(),&family.associatedTypes);
    }
    if (const auto* guide = dyn_cast<CXXDeductionGuideDecl>(f)) {
      auto id = e.owner(guide->getDeducedTemplate()); if (id != noFamily) e.model.unions.emplace_back(current,id);
    }
    if (auto* info = f->getTemplateSpecializationInfo()) for (const auto& a : info->TemplateArguments->asArray()) argument(a,&family.associatedTypes,0);
    if (auto* info = f->getPrimaryTemplate()) if (e.user(info->getBeginLoc()) && e.owner(info) != noFamily) e.model.unions.emplace_back(current,e.owner(info));
    return true;
  }
  bool VisitCXXRecordDecl(CXXRecordDecl* r) {
    if (current == noFamily) return true;
    if (r->hasDefinition() && r == r->getDefinition()) for (const auto& base : r->bases()) type(base.getType());
    if (auto* s = dyn_cast<ClassTemplateSpecializationDecl>(r)) {
      auto& f = e.model.families[current];
      for (const auto& a : s->getTemplateArgs().asArray()) argument(a,&f.associatedTypes,0);
      if (e.user(s->getSpecializedTemplate()->getBeginLoc()) && e.owner(s->getSpecializedTemplate()) != noFamily)
        e.model.unions.emplace_back(current,e.owner(s->getSpecializedTemplate()));
    }
    return true;
  }
  bool VisitVarDecl(VarDecl* v) {
    if (current == noFamily) return true;
    if (auto* s = dyn_cast<VarTemplateSpecializationDecl>(v)) {
      for (const auto& a : s->getTemplateArgs().asArray()) argument(a,&e.model.families[current].associatedTypes,0);
      if (e.user(s->getSpecializedTemplate()->getBeginLoc()) && e.owner(s->getSpecializedTemplate()) != noFamily)
        e.model.unions.emplace_back(current,e.owner(s->getSpecializedTemplate()));
    }
    if (!v->hasGlobalStorage() || (v->isStaticDataMember() && !v->isThisDeclarationADefinition())) return true;
    const bool inert = inertType(v->getType()) && safeSystemMacros(v) && inertExpr(v->getInit());
    if (!inert) e.model.families[current].root = "not inert (static or thread storage)";
    return true;
  }
  void run() {
    // Alias strings name linker symbols, not C++ identifiers. Respect explicit
    // asm labels and the selected target ABI's mangling; ambiguous spellings
    // conservatively root every matching user family.
    std::unique_ptr<MangleContext> mangler(e.compiler.getASTContext().createMangleContext());
    for (const auto& [decl,id] : e.owners) {
      const auto* named = dyn_cast<NamedDecl>(decl);
      if (!named || named->getDeclName().isEmpty() || isa<CXXConstructorDecl,CXXDestructorDecl,CXXDeductionGuideDecl>(decl)) continue;
      const auto* variable = dyn_cast<VarDecl>(decl);
      if (!isa<FunctionDecl>(decl) && (!variable || !variable->hasGlobalStorage())) continue;
      if (const auto* function = dyn_cast<FunctionDecl>(decl); function && function->isDependentContext()) continue;
      if (variable && (variable->getDeclContext()->isDependentContext() || variable->getType()->isDependentType())) continue;
      std::string symbol;
      if (const auto* label = named->getAttr<AsmLabelAttr>()) symbol = label->getLabel().str();
      else if (mangler->shouldMangleDeclName(named)) { llvm::raw_string_ostream out(symbol); mangler->mangleName(named,out); }
      else symbol = named->getNameAsString();
      symbols[symbol].insert(id);
    }
    for (auto [decl,id] : e.declarations) { current = id; TraverseDecl(const_cast<Decl*>(decl)); }
    current = noFamily;
    for (std::size_t i = 0; i < systemPending.size(); ++i) {
      systemCurrent = systems.at(systemPending[i]); TraverseDecl(const_cast<Decl*>(systemPending[i]));
    }
    systemCurrent = noFamily;
  }
};
class Consumer final : public ASTConsumer {
  Extraction& e;
public:
  explicit Consumer(Extraction& x) : e(x) {}
  void HandleTranslationUnit(ASTContext& context) override {
    if (e.compiler.getDiagnostics().hasErrorOccurred()) { e.disable("Clang parse error"); return; }
    for (auto* d : context.getTranslationUnitDecl()->decls()) e.discover(d);
    e.normalizeRanges();
    // Namespaces, redeclarations, overloads and template identities share retention.
    std::map<std::pair<const DeclContext*,std::string>,FamilyId> overloads;
    for (auto [d,id] : e.declarations) {
      if (const auto* t = dyn_cast<TemplateDecl>(d); t && t->getTemplatedDecl()) d = t->getTemplatedDecl();
      if (const auto* f = dyn_cast<FunctionDecl>(d); f && !isa<CXXMethodDecl>(f)) {
        auto key = std::pair{f->getDeclContext()->getPrimaryContext(),f->getNameAsString()};
        auto [it,fresh] = overloads.emplace(key,id); if (!fresh) e.model.unions.emplace_back(id,it->second);
      }
    }
    Ownership ownership(e);
    for (auto [d,id] : e.declarations) { (void)id; ownership.TraverseDecl(const_cast<Decl*>(d)); }
    Dependencies dependencies(e); dependencies.run();
    for (auto [d,id] : e.declarations) {
      for (auto r : e.model.families[id].ranges) for (auto m : e.macroInvocations)
        if ((m.begin < r.begin && r.begin < m.end) || (m.begin < r.end && r.end < m.end))
          e.model.families[id].pinned = "macro invocation boundary";
    }
    // D3b may strip the original comment before extraction. These physical
    // declaration-token associations survive independently of its spelling.
    for (auto offset : e.model.keepOffsets) {
      FamilyId owner = noFamily;
      for (FamilyId id = 0; id < e.model.families.size(); ++id) for (auto r : e.model.families[id].ranges)
        if (r.begin <= offset && offset < r.end) owner = id;
      if (owner == noFamily) e.disable("unbound keep annotation at byte " + std::to_string(offset));
      else e.model.annotations.push_back({offset,owner});
    }
  }
};
} // namespace
Result<PruningModel> extractPruningInWorker(PruningModel model) {
  auto invocation = std::make_shared<CompilerInvocation>();
  auto memory = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  auto overlay = llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(llvm::vfs::getRealFileSystem());
  overlay->pushOverlay(memory);
  for (const auto& resource : resources()) {
    auto path = std::string(pruningResourceDirectory) + "/include/" + resource.path;
    memory->addFile(path,0,llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(reinterpret_cast<const char*>(resource.data),resource.size),path));
  }
  auto diagnostics = CompilerInstance::createDiagnostics(*overlay,invocation->getDiagnosticOpts(),new TextDiagnosticPrinter(llvm::errs(),invocation->getDiagnosticOpts()),true);
  std::vector<const char*> argv; for (const auto& s : model.parseArgv) argv.push_back(s.c_str());
  if (!diagnostics || !CompilerInvocation::CreateFromArgs(*invocation,argv,*diagnostics)) {
    model.disabledReasons.push_back("invalid Clang parse configuration"); return model;
  }
  CompilerInstance compiler(invocation); compiler.setVirtualFileSystem(overlay); compiler.setDiagnostics(diagnostics);
  compiler.createFileManager(); compiler.createSourceManager();
  if (!compiler.createTarget()) { model.disabledReasons.push_back("unsupported Clang target"); return model; }
  compiler.createPreprocessor(TU_Complete); compiler.createASTContext();
  // createPreprocessor initializes the target's builtin table, but registering
  // its names normally happens in FrontendAction::BeginSourceFile. We bypass
  // that action and call ParseAST directly, so perform that step before parsing.
  auto& pp = compiler.getPreprocessor();
  pp.getBuiltinInfo().initializeBuiltins(pp.getIdentifierTable(),compiler.getLangOpts());
  auto& sm = compiler.getSourceManager();
  sm.setMainFileID(sm.createFileID(llvm::MemoryBuffer::getMemBufferCopy(model.text,"flat.cpp")));
  Extraction extraction(std::move(model),compiler);
  auto callbacks = std::make_unique<Macros>(extraction); auto* watcher = callbacks.get();
  compiler.getPreprocessor().setTokenWatcher([watcher](const clang::Token& token) { watcher->finalToken(token); });
  compiler.getPreprocessor().addPPCallbacks(std::move(callbacks));
  Consumer consumer(extraction);
  diagnostics->getClient()->BeginSourceFile(compiler.getLangOpts(),&compiler.getPreprocessor());
  ParseAST(compiler.getPreprocessor(),&consumer,compiler.getASTContext());
  diagnostics->getClient()->EndSourceFile();
  if (diagnostics->hasErrorOccurred()) extraction.disable("Clang parse error");
  for (auto& fact : extraction.model.expansions) if (fact.paste) {
    auto group = extraction.pasteGroups.at(fact.offset); fact.externalPasteOperand = group.second;
    // Argument pre-expansion callbacks may have the argument's physical offset
    // instead of the enclosing invocation's. Join by physical invocation spans.
    for (auto invocation : extraction.macroInvocations) if (invocation.begin <= fact.offset && fact.offset < invocation.end)
      for (const auto& other : extraction.model.expansions) if (invocation.begin <= other.offset && other.offset < invocation.end)
        if (other.predefined || std::find(other.parse.provenance.begin(),other.parse.provenance.end(),FactOrigin::systemHeader) != other.parse.provenance.end()) fact.externalPasteOperand = true;
  }
  auto valid = validatePruningModel(extraction.model); if (!valid.ok()) return valid.error();
  return std::move(extraction.model);
}
} // namespace distill
