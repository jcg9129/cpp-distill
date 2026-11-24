// SPDX-License-Identifier: GPL-3.0-or-later
#include "clang/prune_extract.h"
#include "core/names/names.h"
#include "core/library/library.h"
#include "resources.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
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
bool unspelledFunctionEligible(const FunctionDecl* f) {
  if (isa<CXXMethodDecl,CXXDeductionGuideDecl>(f) ||
      !f->getDeclContext()->getRedeclContext()->isFileContext() ||
      !f->getLexicalDeclContext()->getRedeclContext()->isFileContext() ||
      f->getFriendObjectKind() != Decl::FOK_None || !f->getIdentifier() ||
      f->isMain() || f->getLanguageLinkage() == CLanguageLinkage ||
      f->getTemplateSpecializationKind() != TSK_Undeclared ||
      f->getBeginLoc().isMacroID() || f->getLocation().isMacroID() || f->getEndLoc().isMacroID()) return false;
  // Internal-linkage names have NoLanguageLinkage, but a static function in
  // a C linkage specification still has C function type. The innermost
  // specification governs, including an extern "C++" nested in extern "C".
  for (auto* context = f->getLexicalDeclContext(); context; context = context->getParent())
    if (const auto* linkage = dyn_cast<LinkageSpecDecl>(context)) {
      if (linkage->getLanguage() == LinkageSpecLanguageIDs::C) return false;
      break;
    }
  for (const auto* attr : f->attrs())
    if (isa<AliasAttr,IFuncAttr,WeakAttr,WeakRefAttr,UsedAttr,SectionAttr,
            ConstructorAttr,DestructorAttr,VisibilityAttr,AsmLabelAttr>(attr)) return false;
  return true;
}
bool scalarBuiltin(const Decl* d) {
  const auto* f = dyn_cast<FunctionDecl>(d);
  if (!f || !f->isImplicit() || !f->getBuiltinID()) return false;
  // Clang gives these implicit declarations the user's call-site location.
  // They introduce no further user lookup; arguments are traversed normally.
  // Do not generalize to arbitrary builtins or explicitly declared user code.
  static const std::set<std::string,std::less<>> names = {
    "__builtin_popcount", "__builtin_popcountl", "__builtin_popcountll",
    "__builtin_clz", "__builtin_clzl", "__builtin_clzll",
    "__builtin_ctz", "__builtin_ctzl", "__builtin_ctzll",
    "__builtin_ffs", "__builtin_ffsl", "__builtin_ffsll",
    "__builtin_parity", "__builtin_parityl", "__builtin_parityll",
    "__builtin_bswap16", "__builtin_bswap32", "__builtin_bswap64", "__builtin_bswap128",
    "__builtin_add_overflow", "__builtin_sub_overflow", "__builtin_mul_overflow",
    "__builtin_expect", "__builtin_expect_with_probability", "__builtin_unreachable",
    "__builtin_prefetch"
  };
  return names.contains(f->getNameAsString());
}
struct Extraction {
  PruningModel model;
  CompilerInstance& compiler;
  SourceManager& sm;
  std::map<const Decl*,FamilyId> owners;
  std::map<std::pair<std::size_t,std::string>,FamilyId> memberSpellings;
  std::map<std::pair<std::size_t,std::string>,FamilyId> memberGroups;
  std::set<std::string> virtualMemberNames;
  std::set<FamilyId> explicitlyInstantiatedRecords;
  std::vector<std::pair<const Decl*,FamilyId>> declarations;
  std::map<std::size_t,std::pair<bool,bool>> pasteGroups;
  std::set<std::size_t> hashOffsets; // Expansion offsets of macros defined with #, #@ or ##.
  std::vector<EditRange> macroInvocations, conditionRanges;
  std::vector<distill::Token> tokens;
  std::map<std::size_t,std::set<std::string>> systemMacros;
  // Expanded spelling locations, including macros. Every accepted spelling
  // must also be seen as a NoUniqueAddressAttr on a whole-record-owned field.
  std::map<unsigned,clang::SourceLocation> noUniqueAddressSpellings;
  std::set<unsigned> verifiedNoUniqueAddress;
  explicit Extraction(PruningModel m, CompilerInstance& c) : model(std::move(m)), compiler(c), sm(c.getSourceManager()) {
    auto scan = scanDirectives(model.text);
    if (!scan.ok()) { disable("directive scan failure in flat.cpp: " + scan.error().message); return; }
    for (const auto& l : scan.value().lines) if (!l.directive) tokens.insert(tokens.end(),l.tokens.begin(),l.tokens.end());
    for (const auto& l : scan.value().lines) if (l.directive &&
        (l.directive->name == "if" || l.directive->name == "elif" || l.directive->name == "ifdef" || l.directive->name == "ifndef")) conditionRanges.push_back({l.begin,l.end});
  }
  bool user(clang::SourceLocation loc) const { return loc.isValid() && sm.isWrittenInMainFile(sm.getExpansionLoc(loc)); }
  std::size_t offset(clang::SourceLocation loc) const { return sm.getFileOffset(sm.getExpansionLoc(loc)); }
  // --no-line-markers (docs/reviews/2026-11-18-no-line-markers.md). Bounded:
  // any single use keeps the markers, so later ones add nothing.
  std::set<std::pair<std::size_t,std::string>> positionSeen;
  void position(clang::SourceLocation loc, std::string what) {
    if (!user(loc) || model.positionUses.size() >= 64) return;
    const auto at = offset(loc);
    if (at > model.text.size() || !positionSeen.emplace(at,what).second) return;
    model.positionUses.push_back({at,std::move(what)});
  }
  // A use without a user location (e.g. instantiated from system code) still
  // keeps the markers; it is reported at the start of the text.
  void positionOutside(std::string what) {
    if (model.positionUses.size() >= 64 || !positionSeen.emplace(0,what).second) return;
    model.positionUses.push_back({0,std::move(what)});
  }
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
    // Read the old canonical binding before replacing d: d may itself be the
    // canonical declaration of an already discovered out-of-line definition.
    auto [it,fresh] = owners.emplace(canonical(d),id);
    if (!fresh && it->second != id) {
      if (model.families[id].memberOf != noFamily && model.families[it->second].memberOf == noFamily)
        it->second = id; // Transfer a record-owned method to its own unit.
      else model.unions.emplace_back(id,it->second);
    }
    owners[d] = id;
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
  std::string memberName(const CXXMethodDecl* method) const {
    // Constructors in instances print template arguments; use the written
    // class identifier so they share the pattern's spelling and unit.
    if (isa<CXXConstructorDecl,CXXDestructorDecl>(method) && method->getParent()->getIdentifier())
      return (isa<CXXDestructorDecl>(method) ? "~" : "") + method->getParent()->getIdentifier()->getName().str();
    return method->getNameAsString();
  }
  bool memberUnitsEnabled() const {
    return model.assumeLibrarySpelledMembers && model.libraryMemberNamesComputed &&
        model.memberDisabledReasons.empty();
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
      f.unspelledFunctionEligible = unspelledFunctionEligible(fn);
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
    if (memberUnitsEnabled()) if (const auto* method = dyn_cast<CXXMethodDecl>(base)) {
      const auto parent = owner(method->getParent());
      if (parent != noFamily) {
        f.memberOf = parent; f.memberName = memberName(method);
        // Keep the member spelling separate from namespace-scope names (e.g.
        // a friend declaration). Retention indexes it through memberName.
        // memberOf supplies forward ownership retention. It is not a source
        // dependency: deleting a method cannot delete its containing class
        // definition. Actual references to the owner still add AST/name edges.
        if (isa<CXXConstructorDecl,CXXDestructorDecl,CXXConversionDecl>(method) || method->isOverloadedOperator() || method->isVirtual())
          f.memberBlocker = "D3e rule 1: excluded kind or virtual";
        else if (isReserved(f.memberName)) f.memberBlocker = "D3e rule 3: reserved name";
        // The terminator scan can include shared declarators (int f(), data;)
        // or a nested type introduced in a return type. Those non-function
        // members must stay with the record; pin the indivisible declaration.
        for (const auto* sibling : method->getParent()->decls())
          if (isa<FieldDecl,VarDecl,TagDecl,TypedefNameDecl>(sibling) && user(sibling->getLocation())) {
            const auto at = offset(sibling->getLocation());
            if (r->begin <= at && at < r->end) f.pinned = "shared declaration with non-function member";
          }
      }
    }
    // Inspect the complete written declaration before member extraction or
    // shared-syntax normalization partitions its byte ownership. Boundary
    // pinning alone misses expansions wholly inside a declarator or body.
    if (f.unspelledFunctionEligible)
      for (auto invocation : macroInvocations)
        if (invocation.begin < r->end && r->begin < invocation.end) {
          f.unspelledFunctionEligible = false; break;
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
      if (enclosing != noFamily && enclosing != id && model.families[id].memberOf == noFamily)
        model.unions.emplace_back(id,enclosing);
    }
  }
  void member(Decl* d, CXXMethodDecl* method) {
    if (method->isImplicit() || !user(d->getBeginLoc())) return;
    // A qualified friend declaration belongs to its lexical record's text.
    // Its spelling must retain the referenced member by rule 5, not be
    // carved out as another removable declaration of that member.
    if (method->getFriendObjectKind() != Decl::FOK_None) return;
    // RAV may visit a partial specialization's instances before its written
    // pattern. Seed ownership/ranges from the complete pattern first; an unused
    // lazy instance can lack the body's end location and look unterminated.
    if (method->getTemplateSpecializationKind() != TSK_ExplicitSpecialization)
      if (auto* pattern = dyn_cast_or_null<CXXMethodDecl>(method->getTemplateInstantiationPattern());
          pattern && pattern != method && user(pattern->getBeginLoc())) {
        if (auto* wrapper = pattern->getDescribedFunctionTemplate()) member(wrapper,pattern);
        else member(pattern,pattern);
      }
    // A lazily instantiated method need not have the pattern's body/end loc.
    // The written name token identifies its declaration independently of that
    // local instantiation state; retain the pattern's complete edit range.
    const auto key = std::pair{offset(method->getLocation()),memberName(method)};
    FamilyId id = noFamily;
    if (auto it = memberSpellings.find(key); it != memberSpellings.end()) {
      id = it->second; bind(d,id);
    } else {
      auto declared = std::find_if(declarations.begin(),declarations.end(),[&](const auto& entry) {
        return entry.first == d && model.families[entry.second].memberOf != noFamily;
      });
      if (declared != declarations.end()) id = declared->second;
      else {
        const auto count = model.families.size(); discover(d);
        if (count == model.families.size()) return;
        id = count;
      }
      memberSpellings.emplace(key,id);
    }
    auto& f = model.families[id];
    if (f.memberOf == noFamily) return;
    const auto* record = method->getParent();
    const auto* pattern = record->getTemplateInstantiationPattern();
    const auto at = offset((pattern ? pattern : record)->getBeginLoc());
    auto [it,fresh] = memberGroups.emplace(std::pair{at,f.memberName},id);
    if (!fresh && it->second != id) model.unions.emplace_back(id,it->second);
    if (method->isVirtual() || method->hasAttr<OverrideAttr>() || method->hasAttr<FinalAttr>()) {
      f.memberBlocker = "D3e rule 1: excluded kind or virtual";
      virtualMemberNames.insert(f.memberName);
    }
  }
  void partitionMembers() {
    // Subtract member spellings before the existing shared-declarator union
    // pass, so keeping a record no longer keeps every method in that record.
    std::vector<EditRange> cuts;
    for (const auto& f : model.families) if (f.memberOf != noFamily)
      cuts.insert(cuts.end(),f.ranges.begin(),f.ranges.end());
    std::sort(cuts.begin(),cuts.end(),[](auto a,auto b) { return a.begin < b.begin; });
    for (auto& f : model.families) {
      if (f.memberOf != noFamily) {
        if (f.memberBlocker.empty() && virtualMemberNames.contains(f.memberName))
          f.memberBlocker = "D3e rule 2: virtual name clash";
        if (explicitlyInstantiatedRecords.contains(f.memberOf)) f.memberBlocker = "D3e rule 8: explicitly instantiated record";
      }
      std::vector<EditRange> remaining;
      for (auto r : f.ranges) {
        auto begin = r.begin;
        for (auto c : cuts) if (r.begin <= c.begin && c.end <= r.end && (r.begin != c.begin || r.end != c.end)) {
          if (begin < c.begin) remaining.push_back({begin,c.begin});
          begin = std::max(begin,c.end);
        }
        if (begin < r.end) remaining.push_back({begin,r.end});
      }
      f.ranges = std::move(remaining);
    }
  }
  void normalizeRanges() {
    // Shared declarators are one deletion unit. Union the retention obligations,
    // then partition their byte ownership; never coalesce physical edit ranges.
    struct R { EditRange range; FamilyId id; }; std::vector<R> all;
    for (FamilyId id = 0; id < model.families.size(); ++id) for (auto r : model.families[id].ranges) all.push_back({r,id});
    std::sort(all.begin(),all.end(),[](auto a,auto b) { return a.range.begin < b.range.begin; });
    // D3e records have several disjoint structural fragments. Clear each
    // family once, retaining every fragment when rebuilding byte ownership.
    for (auto& f : model.families) f.ranges.clear();
    std::size_t end = 0; FamilyId prior = noFamily;
    for (auto r : all) {
      auto& f = model.families[r.id];
      if (r.range.begin < end) model.unions.emplace_back(prior,r.id);
      if (r.range.end > end) f.ranges.push_back({std::max(end,r.range.begin),r.range.end});
      if (r.range.end >= end) { end = r.range.end; prior = r.id; }
    }
  }
};

bool supportedAttribute(const Attr* a, const Decl* declaration, Extraction& e) {
  if (isa<OverrideAttr,FinalAttr>(a)) return true;
  if (!isa<NoUniqueAddressAttr>(a)) return supportedAttribute(a);
  const auto* field = dyn_cast_or_null<FieldDecl>(declaration);
  if (!field || field->isBitField() || field->getLexicalDeclContext() != field->getParent()) return false;
  const auto owner = e.owner(field->getParent());
  const auto location = a->getLocation().getRawEncoding();
  if (owner == noFamily || e.owner(field) != owner || !e.noUniqueAddressSpellings.contains(location)) return false;
  e.verifiedNoUniqueAddress.insert(location);
  return true;
}

// Builtin macros whose expansion is the presumed position (rule 1), and
// final tokens that read or print presumed positions (rule 2).
bool positionMacro(std::string_view name) {
  return name == "__LINE__" || name == "__FILE__" || name == "__FILE_NAME__" || name == "__BASE_FILE__" || name == "__INCLUDE_LEVEL__";
}
bool positionToken(std::string_view name) {
  static const std::set<std::string,std::less<>> names = {
    "__builtin_LINE", "__builtin_FILE", "__builtin_FILE_NAME", "__builtin_COLUMN", "__builtin_source_location",
    "__builtin_FUNCSIG", "__PRETTY_FUNCTION__", "__FUNCSIG__", "__FUNCDNAME__", "L__FUNCSIG__", "__builtin_dump_struct"
  };
  return names.contains(name);
}
class Macros final : public PPCallbacks {
  Extraction& e;
  std::map<std::string,MacroFact,std::less<>> sourceBindings;
  std::set<unsigned> systemMacroInvocations;
  std::set<std::string,std::less<>> expandedLibraryMemberNames;
  void libraryToken(const clang::Token& token) {
    if (!e.model.assumeLibrarySpelledMembers || !token.is(tok::identifier) ||
        !token.getLocation().isMacroID()) return;
    // A pasted token's spelling is in scratch space, and an argument's spelling
    // can be user text. Follow BOTH spelling and expansion ancestry. Recording
    // system definitions at MacroExpands also covers a system macro pasted at a
    // user invocation, where neither the scratch spelling nor the call is system.
    std::vector<clang::SourceLocation> pending{token.getLocation()};
    std::set<unsigned> seen;
    while (!pending.empty()) {
      const auto loc = pending.back(); pending.pop_back();
      if (loc.isInvalid() || !seen.insert(loc.getRawEncoding()).second) continue;
      if (systemMacroInvocations.contains(loc.getRawEncoding()) ||
          (loc.isFileID() && e.sm.isInSystemHeader(loc))) {
        expandedLibraryMemberNames.insert(token.getIdentifierInfo()->getName().str());
        return;
      }
      if (seen.size() > 4096) {
        e.model.memberDisabledReasons.push_back("system macro expansion ancestry limit");
        return;
      }
      if (loc.isMacroID()) {
        pending.push_back(e.sm.getImmediateSpellingLoc(loc));
        pending.push_back(e.sm.getImmediateExpansionRange(loc).getBegin());
      }
    }
  }
  enum class AttributeSyntax { none, standard, gnu, declspec };
  AttributeSyntax attributeSyntax = AttributeSyntax::none;
  std::vector<clang::Token> attributeTokens;
  bool previousSquare = false, awaitingParen = false;
  unsigned attributeDepth = 0;
  void auditAttribute() {
    unsigned parens = 0;
    for (std::size_t i = 0; i < attributeTokens.size(); ++i) {
      const auto& token = attributeTokens[i];
      if (token.is(tok::l_paren)) { ++parens; continue; }
      if (token.is(tok::r_paren)) { if (parens) --parens; continue; }
      const unsigned nameDepth = attributeSyntax == AttributeSyntax::standard ? 0 :
                                 attributeSyntax == AttributeSyntax::gnu ? 2 : 1;
      if (parens != nameDepth || !token.is(tok::identifier)) continue;
      const auto name = token.getIdentifierInfo()->getName();
      if (name != "no_unique_address" && name != "__no_unique_address__") continue;
      // Do not infer spelling or argument validity from the AST: Clang can
      // diagnose and discard an attribute (including an unknown vendor form).
      // No pinned Attr.td/AttrDocs are available here to audit the __ spelling.
      const bool plain = attributeSyntax == AttributeSyntax::standard && name == "no_unique_address" &&
          !attributeTokens.front().is(tok::kw_using) &&
          (i == 0 || attributeTokens[i-1].is(tok::comma)) && i+1 < attributeTokens.size() &&
          attributeTokens[i+1].isOneOf(tok::comma,tok::r_square);
      if (plain) e.noUniqueAddressSpellings.emplace(token.getLocation().getRawEncoding(),token.getLocation());
      else e.disable("unsupported attribute: " + name.str() + " at " + token.getLocation().printToString(e.sm));
    }
    attributeTokens.clear(); attributeSyntax = AttributeSyntax::none;
  }
  void attributeToken(const clang::Token& token) {
    if (attributeSyntax != AttributeSyntax::none) {
      attributeTokens.push_back(token);
      const bool standard = attributeSyntax == AttributeSyntax::standard;
      if (awaitingParen) {
        awaitingParen = false;
        if (!token.is(tok::l_paren)) { attributeTokens.clear(); attributeSyntax = AttributeSyntax::none; return; }
      }
      if (token.is(standard ? tok::l_square : tok::l_paren)) ++attributeDepth;
      if (token.is(standard ? tok::r_square : tok::r_paren) && attributeDepth) --attributeDepth;
      if (!attributeDepth) auditAttribute();
      return;
    }
    if (previousSquare && token.is(tok::l_square)) {
      attributeSyntax = AttributeSyntax::standard; attributeDepth = 2;
    } else if (token.isOneOf(tok::kw___attribute,tok::kw___declspec)) {
      attributeSyntax = token.is(tok::kw___attribute) ? AttributeSyntax::gnu : AttributeSyntax::declspec;
      awaitingParen = true; attributeDepth = 0;
    }
    previousSquare = attributeSyntax == AttributeSyntax::none && token.is(tok::l_square);
  }
  MacroFact fact(const MacroInfo* mi) {
    if (!mi || mi->isBuiltinMacro()) return {MacroState::definedUnknown,{}, {}};
    Macro m; m.functionLike = mi->isFunctionLike(); m.variadic = mi->isVariadic();
    for (auto* p : mi->params()) m.parameters.push_back(p->getName().str());
    for (const auto& t : mi->tokens()) { if (!m.replacement.empty()) m.replacement += ' '; m.replacement += e.compiler.getPreprocessor().getSpelling(t); }
    return {MacroState::known,std::move(m),{}};
  }
public:
  std::map<unsigned,FileID> headerFiles;
  explicit Macros(Extraction& x) : e(x) {}
  void PragmaDirective(clang::SourceLocation loc, PragmaIntroducerKind kind) override {
    // The core audits literal #pragma directives. An operator-generated pragma
    // has no such source directive/range, so it needs a separate future audit.
    if (e.user(loc) && kind != PIK_HashPragma) e.disable("unsupported generated user pragma at " + loc.printToString(e.sm));
  }
  void finalToken(const clang::Token& token) {
    libraryToken(token); // Full unpruned TU, before filtering to user expansions.
    if (!e.user(token.getLocation())) return;
    attributeToken(token);
    // Keywords carry identifier info too; annotation and literal tokens don't.
    if (!token.isAnnotation() && !token.isLiteral()) {
      const auto* info = token.getIdentifierInfo();
      if (info && positionToken(std::string_view(info->getName().data(),info->getName().size())))
        e.position(token.getLocation(),info->getName().str());
    }
    if (!token.is(tok::identifier)) return;
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
  void FileChanged(clang::SourceLocation loc, FileChangeReason reason,
                   SrcMgr::CharacteristicKind kind, FileID = FileID()) override {
    if (!e.model.assumeLibrarySpelledMembers || kind == SrcMgr::C_User ||
        (reason != EnterFile && reason != SystemHeaderPragma)) return;
    const auto file = e.sm.getFileID(loc);
    if (file.isInvalid()) {
      e.model.memberDisabledReasons.push_back("unavailable system header for library-spelled identifier set");
      return;
    }
    headerFiles.emplace(file.getHashValue(),file);
  }
  // After parsing, the include graph and system-header classifications are fixed.
  // Raw lexing includes inactive branches and macro replacement lists. Keep this
  // inventory together with the union of all scanned library builds.
  void collectLibraryMemberNames() {
    if (!e.model.assumeLibrarySpelledMembers) return;
    e.model.libraryMemberNames = expandedLibraryMemberNames;
    for (auto name : embeddedLibraryData().memberNames) e.model.libraryMemberNames.emplace(name);
    for (const auto& [key,file] : headerFiles) {
      (void)key;
      bool invalid = false; const auto buffer = e.sm.getBufferData(file,&invalid);
      const auto loc = e.sm.getLocForStartOfFile(file);
      if (invalid) {
        e.model.memberDisabledReasons.push_back("unreadable system header: " + e.sm.getFilename(loc).str());
        continue;
      }
      Lexer lexer(loc,e.compiler.getLangOpts(),buffer.begin(),buffer.begin(),buffer.end());
      clang::Token token;
      bool done = false;
      do {
        done = lexer.LexFromRawLexer(token);
        if (token.is(tok::raw_identifier)) {
          // Decode escaped identifiers and line splices just as the parser does.
          const auto* info = e.compiler.getPreprocessor().LookUpIdentifierInfo(token);
          if (info) e.model.libraryMemberNames.insert(info->getName().str());
          else e.model.memberDisabledReasons.push_back("unreadable identifier in system header: " + e.sm.getFilename(loc).str());
        }
        if (token.is(tok::unknown)) e.model.memberDisabledReasons.push_back("unrecognized token in system header: " + e.sm.getFilename(loc).str());
      } while (!done);
    }
    e.model.libraryMemberNamesComputed = e.model.memberDisabledReasons.empty();
  }
  void MacroDefined(const clang::Token& name, const MacroDirective* md) override {
    if (e.user(name.getLocation())) sourceBindings[name.getIdentifierInfo()->getName().str()] = fact(md->getMacroInfo());
  }
  void MacroUndefined(const clang::Token& name, const MacroDefinition&, const MacroDirective*) override {
    if (e.user(name.getLocation())) sourceBindings[name.getIdentifierInfo()->getName().str()] = {MacroState::undefined,{}, {}};
  }
  void MacroExpands(const clang::Token& token, const MacroDefinition& definition, SourceRange range, const MacroArgs*) override {
    if (e.model.assumeLibrarySpelledMembers) {
      if (const auto* mi = definition.getMacroInfo()) {
        const auto def = e.sm.getSpellingLoc(mi->getDefinitionLoc());
        if (def.isValid() && !e.sm.isWrittenInBuiltinFile(def) &&
            !e.sm.isWrittenInCommandLineFile(def) && e.sm.isInSystemHeader(def))
          systemMacroInvocations.insert(range.getBegin().getRawEncoding());
      } else e.model.memberDisabledReasons.push_back("unavailable macro definition for library-spelled identifier set");
    }
    if (!e.user(range.getBegin())) return;
    const auto* mi = definition.getMacroInfo(); if (!mi) { e.disable("missing macro expansion definition at " + range.getBegin().printToString(e.sm)); return; }
    const auto offset = e.offset(range.getBegin()); const auto name = token.getIdentifierInfo()->getName().str();
    if (positionMacro(name)) e.position(range.getBegin(),name + " expanded"); // Conditions included.
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
    // Rule 5: invoked from user code (name not spelled inside a system header;
    // pasted names count) and not constrained by the standard on the judge.
    if (system && !isStandardMacroName(name) && !e.sm.isInSystemHeader(e.sm.getSpellingLoc(token.getLocation())))
      e.position(range.getBegin(),"non-standard system macro " + name + " expanded");
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
    if (std::any_of(mi->tokens_begin(),mi->tokens_end(),[](const auto& t) { return t.isOneOf(tok::hash,tok::hashat,tok::hashhash); }))
      e.hashOffsets.insert(offset);
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
  std::set<FamilyId> templatedSymbols;
  std::set<FamilyId> callableCarriersSeen;
  bool ownRecordParameter(QualType q) const {
    // Only direct written carriers. Desugaring a trait/alias here could certify
    // a parameter that is an unrelated type on the judge. Pattern and instance
    // checks are intersected, so substitution never improves the certificate.
    // LLVM 23 stores written qualifiers on the type nodes themselves.
    const auto* t = q.getTypePtrOrNull();
    while (t) {
      if (const auto* p = dyn_cast<PointerType>(t)) t = p->getPointeeType().getTypePtr();
      else if (const auto* r = dyn_cast<ReferenceType>(t)) t = r->getPointeeType().getTypePtr();
      else if (const auto* r = dyn_cast<RecordType>(t)) return e.owner(r->getDecl()) == current;
      else if (const auto* r = dyn_cast<InjectedClassNameType>(t)) return e.owner(r->getDecl()) == current;
      else if (const auto* s = dyn_cast<TemplateSpecializationType>(t)) {
        const auto* d = s->getTemplateName().getAsTemplateDecl();
        return isa_and_nonnull<ClassTemplateDecl>(d) && e.owner(d) == current;
      }
      else return false;
    }
    return false;
  }
  // Include member/nested templates, partial specializations, instantiated
  // contexts and out-of-line members. Classification never depends on which
  // specializations the local Clang parse happened to instantiate.
  bool templated(const Decl* d) const {
    for (; d; d = d->getDeclContext() ? Decl::castFromDeclContext(d->getDeclContext()) : nullptr) {
      if (const auto* r = dyn_cast<CXXRecordDecl>(d))
        if (r->getDescribedClassTemplate() || isa<ClassTemplateSpecializationDecl>(r)) return true;
      if (const auto* f = dyn_cast<FunctionDecl>(d))
        if (f->getDescribedFunctionTemplate() || f->getPrimaryTemplate() || f->getInstantiatedFromMemberFunction()) return true;
      if (const auto* v = dyn_cast<VarDecl>(d))
        if (v->getDescribedVarTemplate() || isa<VarTemplateSpecializationDecl>(v)) return true;
      if (const auto* c = dyn_cast<DeclContext>(d); c && c->isDependentContext()) return true;
    }
    return false;
  }
  bool symbolRoot(std::string_view symbol, std::string_view form) {
    auto found = symbols.find(symbol);
    if (found == symbols.end()) {
      e.disable("unresolved " + std::string(form) + " symbol: " + std::string(symbol)); return false;
    }
    for (auto id : found->second) {
      e.model.families[id].root = std::string(form) + " symbol reference";
      if (templatedSymbols.contains(id)) e.model.families[id].instantiationEffect = "template symbol reference";
    }
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
    // LLVM 23's default constructor produces an invalid sentinel, not an
    // empty qualifier. Types without a written qualifier must start null.
    NestedNameSpecifier qualifier = std::nullopt;
    if (const auto* n = dyn_cast<TagType>(t)) qualifier = n->getQualifier();
    else if (const auto* n = dyn_cast<TypedefType>(t)) qualifier = n->getQualifier();
    else if (const auto* n = dyn_cast<UsingType>(t)) qualifier = n->getQualifier();
    // A locally scalar member selection can name a class on another target.
    if (qualifier.getKind() == NestedNameSpecifier::Kind::Type) return false;
    if (const auto* u = dyn_cast<UsingType>(t)) {
      if (!e.user(u->getDecl()->getBeginLoc())) return standardScalarAlias(u->getDecl(),q);
      return inertType(u->desugar(),depth+1);
    }
    if (const auto* a = dyn_cast<TypedefType>(t)) {
      const auto* d = a->getDecl();
      if (!e.user(d->getBeginLoc())) return standardScalarAlias(d,q);
      const auto* info = d->getTypeSourceInfo();
      if (!info) return false;
      const auto* written = info->getTypeLoc().getType().getTypePtr();
      if (!isa<BuiltinType,TypedefType,UsingType>(written)) return false;
      return inertType(info->getTypeLoc().getType(),depth+1);
    }
    // Syntactic types only: no deduced auto, decltype, template or member types.
    return isa<BuiltinType>(t);
  }
  bool standardScalarAlias(const NamedDecl* d, QualType q) const {
    if (!q->isScalarType()) return false;
    auto name = d->getQualifiedNameAsString();
    if (name.starts_with("std::")) name.erase(0,5);
    // The fixed/least/fast-width integer typedefs and size types have scalar
    // types by contract, including the C names imported with using-declarations.
    if (name == "size_t" || name == "ptrdiff_t" || name == "intptr_t" || name == "uintptr_t" ||
        name == "intmax_t" || name == "uintmax_t") return true;
    if (name.starts_with("uint")) name.erase(0,4);
    else if (name.starts_with("int")) name.erase(0,3);
    else return false;
    if (name.starts_with("_least")) name.erase(0,6);
    else if (name.starts_with("_fast")) name.erase(0,5);
    if (!name.ends_with("_t")) return false;
    name.resize(name.size()-2);
    return !name.empty() && std::all_of(name.begin(),name.end(),[](char c) { return c >= '0' && c <= '9'; });
  }
  mutable std::set<const CXXRecordDecl*> trivialParameterRecords;
  bool potentialParameterObject(QualType q, unsigned depth = 0) const {
    if (depth > 50 || q.isNull()) return true;
    const auto* t = q.getTypePtr();
    NestedNameSpecifier qualifier = std::nullopt;
    if (const auto* n = dyn_cast<TagType>(t)) qualifier = n->getQualifier();
    else if (const auto* n = dyn_cast<TypedefType>(t)) qualifier = n->getQualifier();
    else if (const auto* n = dyn_cast<UsingType>(t)) qualifier = n->getQualifier();
    // A selected member can change kind on another target, even when it is an
    // enum or scalar typedef here: Pick<sizeof(long)>::type may name a class
    // in another specialization. Namespace qualifications are harmless.
    if (qualifier.getKind() == NestedNameSpecifier::Kind::Type) return true;
    // Unlike a static variable, an NTTP that is a pointer/reference (even to
    // a dependent or nontrivially destructible class) creates no object of its
    // pointee/referent type. Do not recurse into these types as inertType does.
    if (isa<BuiltinType,EnumType,PointerType,ReferenceType,MemberPointerType>(t)) return false;
    if (const auto* u = dyn_cast<UsingType>(t)) {
      if (!e.user(u->getDecl()->getBeginLoc())) return !standardScalarAlias(u->getDecl(),q);
      return potentialParameterObject(u->desugar(),depth+1);
    }
    if (const auto* a = dyn_cast<TypedefType>(t)) {
      if (!e.user(a->getDecl()->getBeginLoc())) return !standardScalarAlias(a->getDecl(),q);
      return potentialParameterObject(a->desugar(),depth+1);
    }
    if (const auto* p = dyn_cast<ParenType>(t)) return potentialParameterObject(p->getInnerType(),depth+1);
    if (const auto* m = dyn_cast<MacroQualifiedType>(t)) return potentialParameterObject(m->getUnderlyingType(),depth+1);
    if (const auto* s = dyn_cast<SubstTemplateTypeParmType>(t)) return potentialParameterObject(s->getReplacementType(),depth+1);
    if (const auto* a = dyn_cast<ArrayType>(t)) return potentialParameterObject(a->getElementType(),depth+1);
    if (const auto* s = dyn_cast<TemplateSpecializationType>(t)) {
      const auto* d = s->getTemplateName().getAsTemplateDecl();
      // Preserve the written system-template selection even if its canonical
      // type here is scalar: another judge may select a class instead.
      if (d && e.user(d->getBeginLoc()) && s->isTypeAlias())
        return potentialParameterObject(s->getAliasedType(),depth+1);
      return true;
    }
    if (const auto* r = dyn_cast<RecordType>(t)) {
      const auto* d = dyn_cast<CXXRecordDecl>(r->getDecl());
      if (!d || !(d = d->getDefinition()) || !e.user(d->getBeginLoc()) ||
          d->isDependentContext() || isa<ClassTemplateSpecializationDecl>(d) ||
          d->getDescribedClassTemplate() || d->hasAttrs() || !d->hasTrivialDestructor()) return true;
      if (trivialParameterRecords.contains(d)) return false;
      // A locally trivial destructor is insufficient if a base/member type
      // was selected by the system library. Prove those types recursively,
      // memoizing successes so shared member-type DAGs do not expand exponentially.
      for (const auto& b : d->bases()) if (potentialParameterObject(b.getType(),depth+1)) return true;
      for (const auto* f : d->fields()) if (potentialParameterObject(f->getType(),depth+1)) return true;
      trivialParameterRecords.insert(d);
      return false;
    }
    // Keep auto, decltype(auto) and unresolved dependent member types
    // conservative. A target-dependent user branch can bind a class argument
    // absent from all of the worker's known specializations; A1/A6/A9 do not
    // prohibit that. See the review's unseen_argument.cpp counterexample.
    return true;
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
  bool inertConstexpr(const VarDecl* v) {
    // A7 excludes observable limit/cache effects of deleting constant
    // evaluation. This is only an additional storage-root exemption; ordinary
    // traversal, attributes, macro gates and specialization unions still apply.
    if (!v->isConstexpr() || v->getTLSKind() != VarDecl::TLS_None ||
        v->hasAttrs() || v->getType().isVolatileQualified() ||
        v->getType()->isReferenceType() || !safeSystemMacros(v)) return false;
    const VarDecl* written = v;
    if (v->getTemplateSpecializationKind() == TSK_ImplicitInstantiation) {
      // A substituted T must not become an as-written builtin. Explicit
      // specializations instead use their own type and constexpr status.
      if (const auto* pattern = v->getTemplateInstantiationPattern()) written = pattern;
    }
    const auto* info = written->getTypeSourceInfo();
    if (!info || written->hasAttrs() || !safeSystemMacros(written)) return false;
    const auto loc = info->getTypeLoc().getUnqualifiedLoc();
    // Do not desugar aliases, decltype, auto or template-parameter types, even
    // when Clang's selected canonical type is arithmetic or an empty record.
    if (loc.getAs<BuiltinTypeLoc>()) return loc.getType()->isArithmeticType();
    const auto recordLoc = loc.getAs<RecordTypeLoc>();
    if (!recordLoc) return false;
    const auto* record = dyn_cast<CXXRecordDecl>(recordLoc.getDecl());
    if (!record || !(record = record->getDefinition()) || !record->getIdentifier() ||
        !e.user(record->getBeginLoc()) || record->isUnion() || record->isLocalClass() ||
        record->isDependentType() || record->getDescribedClassTemplate() ||
        isa<ClassTemplateSpecializationDecl>(record) || record->hasAttrs() ||
        record->getNumBases() || !record->field_empty() || record->isPolymorphic() ||
        record->hasUserDeclaredConstructor() || record->hasUserDeclaredCopyAssignment() ||
        record->hasUserDeclaredMoveAssignment() || record->hasUserDeclaredDestructor() ||
        !record->hasTrivialDestructor()) return false;
    for (const auto* context = record->getDeclContext(); context; context = context->getParent()) {
      if (context->isDependentContext()) return false;
      if (const auto* parent = dyn_cast<CXXRecordDecl>(context);
          parent && (parent->getDescribedClassTemplate() || isa<ClassTemplateSpecializationDecl>(parent))) return false;
    }
    const auto* list = dyn_cast_or_null<InitListExpr>(v->getInit());
    if (!list) return false;
    if (const auto* syntax = list->getSyntacticForm()) list = syntax;
    // Inspect the written list, not an implicit zero-argument construction
    // that could have originated in a call, conversion or default arguments.
    return list->getNumInits() == 0;
  }
  // These proofs are deliberately separate from rule 1's storage roots.
  // A locally selected specialization or successful constant evaluation is
  // not evidence about a different target. Inspect the pattern's written type.
  const VarDecl* storagePattern(const VarDecl* v) const {
    if (v->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      if (const auto* pattern = v->getTemplateInstantiationPattern()) return pattern;
    return v;
  }
  bool trivialStorageType(QualType q, unsigned depth = 0, bool field = false) {
    if (depth > 50 || q.isNull() || q->isDependentType()) return false;
    const auto* t = q.getTypePtr();
    NestedNameSpecifier qualifier = std::nullopt;
    if (const auto* n = dyn_cast<TagType>(t)) qualifier = n->getQualifier();
    else if (const auto* n = dyn_cast<TypedefType>(t)) qualifier = n->getQualifier();
    else if (const auto* n = dyn_cast<UsingType>(t)) qualifier = n->getQualifier();
    if (qualifier.getKind() == NestedNameSpecifier::Kind::Type) return false;
    if (const auto* u = dyn_cast<UsingType>(t)) {
      if (!e.user(u->getDecl()->getBeginLoc())) return standardScalarAlias(u->getDecl(),q);
      return trivialStorageType(u->desugar(),depth+1,field);
    }
    if (const auto* a = dyn_cast<TypedefType>(t)) {
      const auto* d = a->getDecl();
      if (!e.user(d->getBeginLoc())) return standardScalarAlias(d,q);
      const auto* info = d->getTypeSourceInfo();
      return info && trivialStorageType(info->getTypeLoc().getType(),depth+1,field);
    }
    if (const auto* a = dyn_cast<ConstantArrayType>(t))
      return trivialStorageType(a->getElementType(),depth+1,field);
    if (isa<ReferenceType>(t)) return field; // Reference members have no destructor.
    if (isa<BuiltinType,PointerType,MemberPointerType,EnumType>(t)) return true;
    const auto* rt = dyn_cast<RecordType>(t);
    const auto* r = rt ? dyn_cast<CXXRecordDecl>(rt->getDecl()) : nullptr;
    if (!r || !(r = r->getDefinition()) || !e.user(r->getBeginLoc()) ||
        r->isDependentType() || r->getDescribedClassTemplate() ||
        isa<ClassTemplateSpecializationDecl>(r) || r->hasAttrs() ||
        !r->hasTrivialDestructor()) return false;
    for (const auto* context = r->getDeclContext(); context; context = context->getParent()) {
      if (context->isDependentContext()) return false;
      if (const auto* parent = dyn_cast<CXXRecordDecl>(context);
          parent && (parent->getDescribedClassTemplate() || isa<ClassTemplateSpecializationDecl>(parent))) return false;
    }
    for (const auto& base : r->bases()) if (!trivialStorageType(base.getType(),depth+1)) return false;
    for (const auto* f : r->fields()) if (!trivialStorageType(f->getType(),depth+1,true)) return false;
    return true;
  }
  bool noStorageInstantiationEffect(const VarDecl* variable) {
    const auto* v = storagePattern(variable);
    if (!safeSystemMacros(v) || v->getType()->isDependentType()) return false;
    const auto* init = v->getInit();
    if (init && (init->isTypeDependent() || init->isValueDependent())) return false;
    // This proof also applies without A11, at any scope. Use the
    // syntactic constant-initializer proof, never local constant evaluation.
    // inertExpr also rejects materialized temporaries and default expressions,
    // so this cannot overlook lifetime extension through reference members.
    return inertExpr(init) && trivialStorageType(v->getType());
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
        if (templated(d)) e.model.families[current].instantiationEffect = "template attribute root";
        if (const auto* alias = dyn_cast<AliasAttr>(attr)) symbolRoot(alias->getAliasee().str(),"alias");
      } else if (!supportedAttribute(attr,d,e)) e.disable(std::string("unsupported attribute: ") + attr->getSpelling() + " at " + attr->getLocation().printToString(e.sm));
    }
    if (const auto* a = dyn_cast<FileScopeAsmDecl>(d); a && e.user(d->getBeginLoc())) assembly(a->getAsmString(),d->getBeginLoc());
    return true;
  }
  bool VisitAttributedStmt(AttributedStmt* s) {
    if (current != noFamily && e.user(s->getBeginLoc())) for (const auto* attr : s->getAttrs())
      if (!supportedAttribute(attr,nullptr,e)) e.disable(std::string("unsupported attribute: ") + attr->getSpelling() + " at " + attr->getLocation().printToString(e.sm));
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
  bool memberName(DeclarationName name) {
    if (e.memberUnitsEnabled() && current != noFamily)
      e.model.families[current].dependentNames.insert(name.getAsString());
    return true;
  }
  bool VisitCXXDependentScopeMemberExpr(CXXDependentScopeMemberExpr* x) { return memberName(x->getMember()); }
  bool VisitUnresolvedMemberExpr(UnresolvedMemberExpr* x) {
    for (auto* d : x->decls()) edge(d);
    return memberName(x->getMemberName());
  }
  bool VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr* x) { return memberName(x->getDeclName()); }
  // These names live on declarations, not expressions. In uninstantiated
  // patterns a pasted identifier has neither a raw spelling nor a target edge.
  bool VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl* d) { return memberName(d->getDeclName()); }
  bool VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl* d) { return memberName(d->getDeclName()); }
  bool VisitFunctionDecl(FunctionDecl* f) {
    if (current == noFamily) return true;
    // A qualified dependent friend likewise names a member without creating a
    // member expression or a resolved CXXMethodDecl in the template pattern.
    if (f->getQualifier() && f->getQualifier().isDependent()) memberName(f->getDeclName());
    auto& family = e.model.families[current];
    if (!isa<CXXMethodDecl, CXXDeductionGuideDecl>(f)) {
      // Written local externs, friends and specializations join these families
      // and veto eligibility. Lambda call operators are expression-owned, not
      // namespace-function redeclarations. Implicit instances add no spelling.
      if (!f->isImplicit() && e.user(f->getBeginLoc()) &&
          f->getTemplateSpecializationKind() != TSK_ImplicitInstantiation) {
        const bool eligible = unspelledFunctionEligible(f);
        family.unspelledFunctionEligible &= eligible;
        // A block extern's physical spelling belongs to the enclosing
        // function, while its canonical declaration belongs to the namespace
        // function. Intersect that certificate too, independently of the
        // shared-syntax union that already preserves retention.
        if (const auto it = e.owners.find(f->getCanonicalDecl()); it != e.owners.end())
          e.model.families[it->second].unspelledFunctionEligible &= eligible;
      }
      bool carrier = false;
      for (const auto* p : f->parameters()) carrier |= ownRecordParameter(p->getType());
      if (callableCarriersSeen.insert(current).second) family.boundedSystemHooks = carrier;
      else family.boundedSystemHooks &= carrier;
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
  bool VisitFriendDecl(FriendDecl* d) {
    if (!e.model.assumeInstantiationEffects && current != noFamily && e.user(d->getBeginLoc()) && templated(d) &&
        isa_and_nonnull<FunctionDecl,FunctionTemplateDecl>(d->getFriendDecl()))
      e.model.families[current].instantiationEffect = "template friend declaration";
    return true;
  }
  bool VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl* d) {
    // Class-valued NTTPs introduce static template parameter objects, not
    // VarDecls. Use a separate type proof: the static-variable whitelist both
    // rejects safe scalar sugar and incorrectly examines pointer pointees.
    if (!e.model.assumeInstantiationEffects && current != noFamily && e.user(d->getBeginLoc()) && potentialParameterObject(d->getType()))
      e.model.families[current].instantiationEffect = "potential template parameter object storage";
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
    const bool inert = (inertType(v->getType()) && safeSystemMacros(v) && inertExpr(v->getInit())) || inertConstexpr(v);
    if (!inert) {
      e.model.families[current].root = "not inert (static or thread storage)";
      // A11 excludes constexpr destruction (including extended temporaries)
      // and destruction of block storage whose declaration is never reached.
      // Ordinary dependencies preserve reached declarations; rule 1 is unchanged.
      if (templated(v) &&
          !(e.model.assumeInstantiationEffects && (v->isConstexpr() || v->isLocalVarDecl())) &&
          !noStorageInstantiationEffect(v))
        e.model.families[current].instantiationEffect = "template static or thread storage not inert";
    }
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
      if (templated(named)) templatedSymbols.insert(id);
    }
    for (auto [decl,id] : e.declarations) { current = id; TraverseDecl(const_cast<Decl*>(decl)); }
    current = noFamily;
    for (std::size_t i = 0; i < systemPending.size(); ++i) {
      systemCurrent = systems.at(systemPending[i]); TraverseDecl(const_cast<Decl*>(systemPending[i]));
    }
    systemCurrent = noFamily;
  }
};
// --no-line-markers rules 3 and 4 (docs/reviews/2026-11-18-no-line-markers.md).
bool stdRoot(const DeclContext* c) {
  const NamespaceDecl* outer = nullptr;
  for (; c && !c->isTranslationUnit(); c = c->getParent())
    if (const auto* ns = dyn_cast<NamespaceDecl>(c); ns && !ns->isInline()) outer = ns;
  return outer && outer->getName() == "std";
}
// std::meta or std::contracts; inline (versioning) namespaces are transparent.
bool positionNamespace(const DeclContext* c) {
  std::vector<llvm::StringRef> names;
  for (; c && !c->isTranslationUnit(); c = c->getParent())
    if (const auto* ns = dyn_cast<NamespaceDecl>(c); ns && !ns->isInline()) names.push_back(ns->getName());
  return names.size() >= 2 && names.back() == "std" && (names[names.size()-2] == "meta" || names[names.size()-2] == "contracts");
}
bool positionRecord(const RecordDecl* r) {
  if (!r || !r->getIdentifier()) return false;
  const auto name = r->getName();
  return (name == "source_location" || name == "basic_stacktrace" || name == "stacktrace_entry" || name == "contract_violation") &&
      stdRoot(r->getDeclContext());
}
bool positionEntity(const Decl* d) {
  if (!d) return false;
  if (const auto* r = dyn_cast<RecordDecl>(d); r && positionRecord(r)) return true;
  if (const auto* ns = dyn_cast<NamespaceDecl>(d); ns && positionNamespace(ns)) return true;
  for (const DeclContext* c = d->getDeclContext(); c && !c->isTranslationUnit(); c = c->getParent())
    if (const auto* r = dyn_cast<RecordDecl>(c); r && positionRecord(r)) return true;
  return positionNamespace(d->getDeclContext());
}
bool positionType(QualType t, unsigned depth = 0) {
  if (t.isNull()) return false;
  if (depth > 8) return true;
  t = t.getNonReferenceType();
  if (const auto* p = t->getAs<PointerType>()) return positionType(p->getPointeeType(),depth+1);
  if (const auto* m = t->getAs<MemberPointerType>()) return positionType(m->getPointeeType(),depth+1);
  if (const auto* a = t->getAsArrayTypeUnsafe()) return positionType(a->getElementType(),depth+1);
  if (const auto* f = t->getAs<FunctionType>()) return positionType(f->getReturnType(),depth+1);
  return positionEntity(t->getAsTagDecl());
}
// Builtin calls whose result can contain a type name, function name or source
// position (Clang prints unnamed types and closures with their presumed
// location). When unsure, a name is included: a false hit only keeps markers.
bool namingBuiltin(llvm::StringRef name) {
  for (const char* part : {"dump", "name", "FUNC", "FILE", "LINE", "COLUMN", "source_location", "sycl"})
    if (name.contains(part)) return true;
  return false;
}
// Codex reviews 1-3: hand-written partial walks (user code, instantiations,
// local classes) each missed a route by which a function name or position
// reaches the program (closures and unnamed types print with their presumed
// location). One RecursiveASTVisitor over the whole translation unit,
// patterns and instantiations, implicit code, lambdas, local classes,
// parameter types and attributes, and every evaluated default argument/
// initializer, records ANY function-name
// expression, source-location builtin, unique-stable-name expression or
// naming builtin call, wherever it is: no filtering by location, user-ness
// or template arguments. The only thing not traversed is a parameter's
// default argument as a declaration: it is evaluated only through a
// CXXDefaultArgExpr at a call, which is traversed (so an unused
// std::source_location::current() declaration doesn't count, a call does).
class NameExpressions final : public RecursiveASTVisitor<NameExpressions> {
  Extraction& e;
  std::size_t visits = 0;
  std::set<const Expr*> defaults;
  void use(clang::SourceLocation loc, const std::string& what) {
    if (e.user(loc)) e.position(loc,what);
    else e.positionOutside(what + " (outside user code" + (loc.isValid() ? ": " + loc.printToString(e.sm) : std::string()) + ")");
  }
  bool evaluated(const Expr* x) {
    if (!x || !defaults.insert(x).second) return true;
    return TraverseStmt(const_cast<Expr*>(x));
  }
public:
  explicit NameExpressions(Extraction& x) : e(x) {}
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
  bool TraverseParmVarDecl(ParmVarDecl* d) {
    if (!WalkUpFromParmVarDecl(d)) return false;
    if (auto* t = d->getTypeSourceInfo()) {
      if (!TraverseTypeLoc(t->getTypeLoc())) return false;
    } else if (!TraverseType(d->getType())) return false;
    for (auto* a : d->attrs()) if (!TraverseAttr(a)) return false;
    return true; // Default arguments are traversed only at calls below.
  }
  bool VisitStmt(Stmt*) {
    // Fail closed: past the bound, stop and keep the markers.
    if (++visits <= 50000000) return true;
    e.positionOutside("AST scan limit for function names and positions");
    return false;
  }
  bool VisitPredefinedExpr(PredefinedExpr* x) { use(x->getBeginLoc(),"function name expression"); return true; }
  bool VisitSourceLocExpr(SourceLocExpr* x) { use(x->getBeginLoc(),"source position builtin"); return true; }
  bool VisitSYCLUniqueStableNameExpr(SYCLUniqueStableNameExpr* x) { use(x->getBeginLoc(),"unique stable name expression"); return true; }
  bool VisitCallExpr(CallExpr* x) {
    const auto* callee = x->getDirectCallee();
    if (callee && callee->getBuiltinID() && callee->getIdentifier() && namingBuiltin(callee->getName()))
      use(x->getBeginLoc(),"builtin " + callee->getName().str());
    return true;
  }
  // RAV may or may not descend into these (version-dependent); do it here.
  bool VisitCXXDefaultArgExpr(CXXDefaultArgExpr* x) {
    if (!evaluated(x->getExpr())) return false;
    const auto* p = x->getParam();
    if (p && p->hasDefaultArg() && !p->hasUnparsedDefaultArg() && !p->hasUninstantiatedDefaultArg()) return evaluated(p->getDefaultArg());
    return true;
  }
  bool VisitCXXDefaultInitExpr(CXXDefaultInitExpr* x) {
    if (!evaluated(x->getExpr())) return false;
    return !x->getField() || evaluated(x->getField()->getInClassInitializer());
  }
};
class Positions final : public RecursiveASTVisitor<Positions> {
  Extraction& e;
public:
  explicit Positions(Extraction& x) : e(x) {}
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
  bool VisitExpr(Expr* x) {
    if (e.user(x->getExprLoc()) && positionType(x->getType())) e.position(x->getExprLoc(),"value of type " + x->getType().getAsString());
    return true;
  }
  bool VisitValueDecl(ValueDecl* d) {
    if (e.user(d->getLocation()) && positionType(d->getType())) e.position(d->getLocation(),"declaration of type " + d->getType().getAsString());
    return true;
  }
  bool VisitTypeLoc(TypeLoc t) {
    if (e.user(t.getBeginLoc()) && positionType(t.getType())) e.position(t.getBeginLoc(),"type " + t.getType().getAsString());
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr* x) {
    if (positionEntity(x->getDecl())) e.position(x->getLocation(),"use of " + x->getDecl()->getQualifiedNameAsString());
    return true;
  }
  bool VisitMemberExpr(MemberExpr* x) {
    if (positionEntity(x->getMemberDecl())) e.position(x->getMemberLoc(),"use of " + x->getMemberDecl()->getQualifiedNameAsString());
    return true;
  }
  bool VisitUsingDirectiveDecl(UsingDirectiveDecl* d) {
    if (positionEntity(d->getNominatedNamespace())) e.position(d->getLocation(),"using directive for a namespace with source positions");
    return true;
  }
  bool VisitUsingShadowDecl(UsingShadowDecl* d) {
    if (positionEntity(d->getTargetDecl())) e.position(d->getLocation(),"using declaration of " + d->getTargetDecl()->getQualifiedNameAsString());
    return true;
  }
  bool VisitNamespaceAliasDecl(NamespaceAliasDecl* d) {
    if (positionEntity(d->getNamespace())) e.position(d->getLocation(),"alias of a namespace with source positions");
    return true;
  }
};
// Visit patterns and known instantiations before byte ownership is finalized.
// Semantic virtualness in an instance can exclude an otherwise nonvirtual pattern.
class MemberUnits final : public RecursiveASTVisitor<MemberUnits> {
  Extraction& e;
public:
  explicit MemberUnits(Extraction& x) : e(x) {}
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool VisitFunctionDecl(FunctionDecl* f) {
    auto* method = dyn_cast<CXXMethodDecl>(f);
    if (!method) return true;
    if (auto* wrapper = method->getDescribedFunctionTemplate()) e.member(wrapper,method);
    else e.member(method,method);
    return true;
  }
  bool VisitClassTemplateSpecializationDecl(ClassTemplateSpecializationDecl* r) {
    if (!e.user(r->getBeginLoc())) return true;
    const auto kind = r->getSpecializationKind();
    if (kind == TSK_ExplicitInstantiationDeclaration || kind == TSK_ExplicitInstantiationDefinition) {
      for (const Decl* d : {static_cast<const Decl*>(r),static_cast<const Decl*>(r->getSpecializedTemplate()),
                            static_cast<const Decl*>(r->getTemplateInstantiationPattern())}) {
        auto owner = e.owner(d);
        if (owner != noFamily) e.explicitlyInstantiatedRecords.insert(owner);
      }
    }
    return true;
  }
};
class Consumer final : public ASTConsumer {
  Extraction& e;
public:
  Macros& macros;
  explicit Consumer(Extraction& x, Macros& m) : e(x), macros(m) {}
  void HandleTranslationUnit(ASTContext& context) override {
    macros.collectLibraryMemberNames();
    if (e.compiler.getDiagnostics().hasErrorOccurred()) { e.disable("Clang parse error"); return; }
    for (auto* d : context.getTranslationUnitDecl()->decls()) e.discover(d);
    if (e.memberUnitsEnabled()) {
      MemberUnits(e).TraverseDecl(context.getTranslationUnitDecl());
      e.partitionMembers();
    }
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
    Positions positions(e); // Whole user TU, independent of family discovery.
    for (auto* d : context.getTranslationUnitDecl()->decls()) if (e.user(d->getLocation())) positions.TraverseDecl(d);
    NameExpressions(e).TraverseDecl(context.getTranslationUnitDecl()); // Whole TU, no filtering.
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
  model.parseWidths = LiteralWidths{compiler.getTarget().getIntWidth(),compiler.getTarget().getLongWidth(),compiler.getTarget().getLongLongWidth()};
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
  Consumer consumer(extraction,*watcher);
  diagnostics->getClient()->BeginSourceFile(compiler.getLangOpts(),&compiler.getPreprocessor());
  ParseAST(compiler.getPreprocessor(),&consumer,compiler.getASTContext());
  diagnostics->getClient()->EndSourceFile();
  if (diagnostics->hasErrorOccurred()) extraction.disable("Clang parse error");
  for (const auto& [location,source] : extraction.noUniqueAddressSpellings)
    if (!extraction.verifiedNoUniqueAddress.contains(location))
      extraction.disable("unsupported attribute: no_unique_address at " + source.printToString(sm));
  for (auto& fact : extraction.model.expansions) if (fact.paste) {
    auto group = extraction.pasteGroups.at(fact.offset); fact.externalPasteOperand = group.second;
    // Argument pre-expansion callbacks may have the argument's physical offset
    // instead of the enclosing invocation's. Join by physical invocation spans.
    for (auto invocation : extraction.macroInvocations) if (invocation.begin <= fact.offset && fact.offset < invocation.end)
      for (const auto& other : extraction.model.expansions) if (invocation.begin <= other.offset && other.offset < invocation.end)
        if (other.predefined || std::find(other.parse.provenance.begin(),other.parse.provenance.end(),FactOrigin::systemHeader) != other.parse.provenance.end()) fact.externalPasteOperand = true;
  }
  // D3d-4: a literal's spelling may reach a # or ## operand only through a
  // macro expanded within the same physical invocation span (see the review).
  std::vector<EditRange> hashSpans;
  for (auto invocation : extraction.macroInvocations) {
    auto i = extraction.hashOffsets.lower_bound(invocation.begin);
    if (i != extraction.hashOffsets.end() && *i < invocation.end) hashSpans.push_back(invocation);
  }
  std::sort(hashSpans.begin(),hashSpans.end(),[](auto a, auto b) { return a.begin < b.begin; });
  std::vector<EditRange> merged;
  for (auto r : hashSpans) if (!merged.empty() && r.begin <= merged.back().end) merged.back().end = std::max(merged.back().end,r.end); else merged.push_back(r);
  for (auto& fact : extraction.model.expansions) {
    auto i = std::upper_bound(merged.begin(),merged.end(),fact.offset,[](std::size_t o, auto r) { return o < r.begin; });
    fact.hashOperator = extraction.hashOffsets.contains(fact.offset) || (i != merged.begin() && fact.offset < std::prev(i)->end);
  }
  auto valid = validatePruningModel(extraction.model); if (!valid.ok()) return valid.error();
  return std::move(extraction.model);
}
} // namespace distill
