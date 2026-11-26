// SPDX-License-Identifier: GPL-3.0-or-later
// Included inside prune_extract.cpp's anonymous namespace. Keep the analysis
// separate from ownership: a nested class does not inherit its owner's reachability.
class LibraryReachability final : public RecursiveASTVisitor<LibraryReachability> {
  Extraction& e;
  using Record = const CXXRecordDecl*;
  std::map<Record,std::size_t> classes;
  std::map<Record,std::vector<Record>> instances;
  std::set<Record> templateCandidates;
  bool dependentBoundary = false, opaqueDependentBoundary = false, userContext = false;
  std::size_t visits = 0;
  Record key(Record r) const {
    for (unsigned n = 0; r && n < 100; ++n) {
      if (r->isInjectedClassName()) {
        r = dyn_cast<CXXRecordDecl>(r->getDeclContext()); continue;
      }
      if (const auto* s = dyn_cast<ClassTemplateSpecializationDecl>(r)) {
        if (system(s->getSpecializedTemplate())) return r->getCanonicalDecl();
        r = s->getSpecializedTemplate()->getTemplatedDecl(); continue;
      }
      if (const auto* p = r->getInstantiatedFromMemberClass(); p && p != r) { r = p; continue; }
      return r->getCanonicalDecl();
    }
    return nullptr;
  }
  bool system(const Decl* d) const {
    return d && d->getLocation().isValid() && e.sm.isInSystemHeader(d->getLocation());
  }
  std::size_t index(Record r) {
    r = key(r);
    if (!r) { e.model.libraryReachabilityFailure = "unclassifiable record identity"; return noFamily; }
    auto [it,fresh] = classes.emplace(r,e.model.libraryClasses.size());
    if (fresh) {
      auto label = r->getQualifiedNameAsString();
      if (label.empty()) label = e.describe(r);
      e.model.libraryClasses.push_back({std::move(label),{},e.user(r->getLocation())});
    }
    return it->second;
  }
  void mark(Record r, const std::string& why) {
    const auto id = index(r);
    if (id != noFamily && e.model.libraryClasses[id].reason.empty()) e.model.libraryClasses[id].reason = why;
  }
  // Traverse both written sugar and canonical types. In particular alias template
  // arguments can disappear on desugaring, and function/member-pointer/pack types
  // can mention a record without being a RecordType themselves.
  void type(QualType q, std::set<Record>& out, std::set<const Type*>& seen, unsigned depth = 0) {
    if (q.isNull()) return;
    if (depth > 100) { e.model.libraryReachabilityFailure = "type traversal limit"; return; }
    const auto* t = q.getTypePtr();
    if (!seen.insert(t).second) return;
    auto recurse = [&](QualType child) { type(child,out,seen,depth+1); };
    if (const auto* r = dyn_cast<RecordType>(t)) {
      if (const auto* c = dyn_cast<CXXRecordDecl>(r->getDecl())) {
        out.insert(key(c));
        if (const auto* s = dyn_cast<ClassTemplateSpecializationDecl>(c))
          for (const auto& a : s->getTemplateArgs().asArray()) argument(a,out,seen,depth+1);
      }
    } else if (const auto* r = dyn_cast<InjectedClassNameType>(t)) out.insert(key(r->getDecl()));
    if (const auto* s = dyn_cast<TemplateSpecializationType>(t)) {
      if (const auto* c = dyn_cast_or_null<ClassTemplateDecl>(s->getTemplateName().getAsTemplateDecl()))
        out.insert(key(c->getTemplatedDecl()));
      for (const auto& a : s->template_arguments()) argument(a,out,seen,depth+1);
    }
    if (const auto* p = dyn_cast<PointerType>(t)) recurse(p->getPointeeType());
    else if (const auto* p = dyn_cast<ReferenceType>(t)) recurse(p->getPointeeType());
    else if (const auto* p = dyn_cast<MemberPointerType>(t)) {
      recurse(p->getPointeeType());
      const auto qualifier = p->getQualifier();
      if (qualifier.getKind() == NestedNameSpecifier::Kind::Type) recurse(QualType(qualifier.getAsType(),0));
    }
    else if (const auto* a = dyn_cast<ArrayType>(t)) recurse(a->getElementType());
    else if (const auto* a = dyn_cast<AtomicType>(t)) recurse(a->getValueType());
    else if (const auto* f = dyn_cast<FunctionType>(t)) {
      recurse(f->getReturnType());
      if (const auto* p = dyn_cast<FunctionProtoType>(f)) for (auto param : p->param_types()) recurse(param);
    } else if (const auto* p = dyn_cast<PackExpansionType>(t)) recurse(p->getPattern());
    const auto desugared = q.getSingleStepDesugaredType(e.compiler.getASTContext());
    if (desugared.getTypePtr() != t) recurse(desugared);
  }
  void argument(const TemplateArgument& a, std::set<Record>& out, std::set<const Type*>& seen, unsigned depth = 0) {
    if (depth > 100) { e.model.libraryReachabilityFailure = "template argument traversal limit"; return; }
    switch (a.getKind()) {
    case TemplateArgument::Type: type(a.getAsType(),out,seen,depth+1); break;
    case TemplateArgument::Pack:
      for (const auto& item : a.pack_elements()) argument(item,out,seen,depth+1);
      break;
    case TemplateArgument::Declaration:
      type(a.getParamTypeForDecl(),out,seen,depth+1);
      if (const auto* method = dyn_cast<CXXMethodDecl>(a.getAsDecl())) out.insert(key(method->getParent()));
      break;
    case TemplateArgument::Expression: type(a.getAsExpr()->getType(),out,seen,depth+1); break;
    case TemplateArgument::NullPtr: type(a.getNullPtrType(),out,seen,depth+1); break;
    case TemplateArgument::Integral: type(a.getIntegralType(),out,seen,depth+1); break;
    case TemplateArgument::StructuralValue: type(a.getStructuralValueType(),out,seen,depth+1); break;
    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion:
      if (const auto* t = dyn_cast_or_null<ClassTemplateDecl>(a.getAsTemplateOrTemplatePattern().getAsTemplateDecl())) out.insert(key(t->getTemplatedDecl()));
      break;
    default: break; // Null arguments carry no class value.
    }
  }
  std::set<Record> records(QualType q) {
    std::set<Record> out; std::set<const Type*> seen; type(q,out,seen); out.erase(nullptr); return out;
  }
  void reach(QualType q, const std::string& why) { for (auto r : records(q)) mark(r,why); }
  bool opaqueDependentType(QualType q, unsigned depth = 0) const {
    if (q.isNull() || !q->isDependentType()) return false;
    if (depth > 100) return true;
    const auto* t = q.getTypePtr();
    if (isa<TemplateTypeParmType,InjectedClassNameType>(t)) return false;
    auto opaque = [&](QualType child) { return opaqueDependentType(child,depth+1); };
    const auto desugared = q.getSingleStepDesugaredType(e.compiler.getASTContext());
    if (desugared.getTypePtr() != t) return opaque(desugared);
    if (const auto* p = dyn_cast<PointerType>(t)) return opaque(p->getPointeeType());
    if (const auto* p = dyn_cast<ReferenceType>(t)) return opaque(p->getPointeeType());
    if (const auto* a = dyn_cast<ArrayType>(t)) return opaque(a->getElementType());
    if (const auto* p = dyn_cast<PackExpansionType>(t)) return opaque(p->getPattern());
    if (const auto* f = dyn_cast<FunctionType>(t)) {
      if (opaque(f->getReturnType())) return true;
      if (const auto* p = dyn_cast<FunctionProtoType>(f)) for (auto param : p->param_types()) if (opaque(param)) return true;
      return false;
    }
    if (const auto* s = dyn_cast<TemplateSpecializationType>(t)) {
      for (const auto& a : s->template_arguments()) if (opaqueDependentArgument(a,depth+1)) return true;
      return false;
    }
    // A dependent selector/decltype can introduce a class that was never itself
    // an argument of the enclosing user template (e.g. T::type names an alias).
    // No recorded substitution proves a bound on that set: keep all user classes.
    return true;
  }
  bool opaqueDependentArgument(const TemplateArgument& a, unsigned depth = 0) const {
    if (depth > 100) return true;
    if (a.getKind() == TemplateArgument::Type) return opaqueDependentType(a.getAsType(),depth+1);
    if (a.getKind() == TemplateArgument::Expression) return opaqueDependentType(a.getAsExpr()->getType(),depth+1);
    if (a.getKind() == TemplateArgument::Pack)
      for (const auto& item : a.pack_elements()) if (opaqueDependentArgument(item,depth+1)) return true;
    return false;
  }
  void arguments(ArrayRef<TemplateArgument> args, const Decl* templ) {
    const bool boundary = system(templ);
    for (const auto& a : args) {
      std::set<Record> out; std::set<const Type*> seen; argument(a,out,seen); out.erase(nullptr);
      if (boundary) {
        for (auto r : out) mark(r,"system template argument: " + cast<NamedDecl>(templ)->getQualifiedNameAsString());
        dependentBoundary |= userContext && a.isDependent();
        if (userContext) opaqueDependentBoundary |= opaqueDependentArgument(a);
      } else templateCandidates.insert(out.begin(),out.end());
    }
  }
public:
  explicit LibraryReachability(Extraction& x) : e(x) {}
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
  bool TraverseDecl(Decl* d) {
    if (!d) return true;
    const bool saved = userContext; userContext = e.user(d->getLocation());
    const bool result = RecursiveASTVisitor::TraverseDecl(d); userContext = saved; return result;
  }
  bool VisitDecl(Decl*) { return ++visits <= 2000000; }
  bool VisitStmt(Stmt*) { return ++visits <= 2000000; }
  bool VisitCXXRecordDecl(CXXRecordDecl* r) {
    if (e.user(r->getLocation())) index(r);
    instances[key(r)].push_back(r);
    if (r->isLambda()) mark(r,"closure type");
    if (system(r)) mark(r,"declared in a system header");
    if (const auto* s = dyn_cast<ClassTemplateSpecializationDecl>(r)) {
      arguments(s->getTemplateArgs().asArray(),s->getSpecializedTemplate());
      if (system(s->getSpecializedTemplate())) mark(r,"specialization of a system class template");
    }
    return true;
  }
  bool VisitTemplateSpecializationType(TemplateSpecializationType* t) {
    arguments(t->template_arguments(),t->getTemplateName().getAsTemplateDecl()); return true;
  }
  bool VisitFunctionDecl(FunctionDecl* f) {
    if (const auto* args = f->getTemplateSpecializationArgs()) arguments(args->asArray(),f->getPrimaryTemplate());
    return true;
  }
  bool VisitVarTemplateSpecializationDecl(VarTemplateSpecializationDecl* v) {
    arguments(v->getTemplateArgs().asArray(),v->getSpecializedTemplate()); return true;
  }
  bool VisitConceptSpecializationExpr(ConceptSpecializationExpr* x) {
    arguments(x->getTemplateArguments(),x->getNamedConcept()); return true;
  }
  bool VisitCallExpr(CallExpr* x) {
    bool boundary = system(x->getDirectCallee());
    if (const auto* ref = dyn_cast<DeclRefExpr>(x->getCallee()->IgnoreParenImpCasts())) boundary |= system(ref->getDecl());
    if (boundary) for (const auto* a : x->arguments()) {
      reach(a->getType(),"argument of a system call");
      reach(a->IgnoreParenImpCasts()->getType(),"argument of a system call before conversion");
    }
    // Unresolved dependent calls may select a system overload or customization.
    // Conservatively include all classes recorded as user-template arguments.
    if (userContext && (x->isTypeDependent() || x->isValueDependent())) dependentBoundary = true;
    return true;
  }
  bool VisitCXXConstructExpr(CXXConstructExpr* x) {
    if (system(x->getConstructor())) for (const auto* a : x->arguments()) {
      reach(a->getType(),"argument of a system constructor");
      reach(a->IgnoreParenImpCasts()->getType(),"argument of a system constructor before conversion");
    }
    return true;
  }
  bool VisitCXXForRangeStmt(CXXForRangeStmt* x) { reach(x->getRangeInit()->getType(),"range-for range"); return true; }
  bool VisitDecompositionDecl(DecompositionDecl* x) {
    if (x->getInit()) reach(x->getInit()->getType(),"structured binding initializer");
    return true;
  }
  bool VisitCXXTypeidExpr(CXXTypeidExpr* x) {
    reach(x->isTypeOperand() ? x->getTypeOperand(e.compiler.getASTContext()) : x->getExprOperand()->getType(),"typeid operand"); return true;
  }
  bool VisitCXXNewExpr(CXXNewExpr* x) { reach(x->getAllocatedType(),"allocation protocol"); return true; }
  bool VisitCXXDeleteExpr(CXXDeleteExpr* x) { reach(x->getArgument()->getType(),"deallocation protocol"); return true; }
  bool VisitExpr(Expr* x) {
    if (isa<CoawaitExpr,CoyieldExpr>(x)) reach(x->getType(),"coroutine protocol");
    return true;
  }
  void run(ASTContext& context) {
    if (context.getExternalSource()) e.model.libraryReachabilityFailure = "external AST source is not fully classified";
    if (!TraverseDecl(context.getTranslationUnitDecl())) e.model.libraryReachabilityFailure = "AST traversal limit";
    if (dependentBoundary) for (auto r : templateCandidates) mark(r,"conservative dependent library use (recorded user-template argument)");
    if (opaqueDependentBoundary) for (const auto& [r,id] : classes)
      if (e.model.libraryClasses[id].user) mark(r,"unclassified dependent system-template type argument");
    // Close over bases and implicit conversions in both directions. Inspect every
    // known specialization: the class-template pattern is the unit of reachability.
    std::vector<std::pair<Record,Record>> conversions, bases;
    std::set<Record> unknownBases;
    for (const auto& [pattern,decls] : instances) for (const auto* r : decls) if (r->isCompleteDefinition()) {
      for (const auto& b : r->bases()) {
        const auto involved = records(b.getType());
        // Pattern reachability includes incomplete specializations whose bases
        // Clang has not instantiated. A dependent base can then name any user
        // class (including one selected through a dependent alias).
        if (e.user(r->getLocation()) && b.getType()->isDependentType() &&
            (involved.empty() || opaqueDependentType(b.getType()))) unknownBases.insert(pattern);
        for (auto base : involved) {
          bases.emplace_back(pattern,base); conversions.emplace_back(pattern,base);
        }
      }
      for (const auto* declaration : r->decls()) {
        const Decl* d = declaration;
        if (const auto* t = dyn_cast<FunctionTemplateDecl>(d)) d = t->getTemplatedDecl();
        if (const auto* c = dyn_cast<CXXConversionDecl>(d))
          for (auto target : records(c->getConversionType())) conversions.emplace_back(pattern,target);
        if (const auto* c = dyn_cast<CXXConstructorDecl>(d)) for (const auto* p : c->parameters())
          for (auto source : records(p->getType())) conversions.emplace_back(pattern,source);
      }
    }
    auto reachable = [&](Record r) { auto it = classes.find(r); return it != classes.end() && !e.model.libraryClasses[it->second].reason.empty(); };
    bool changed;
    do {
      const auto before = std::count_if(e.model.libraryClasses.begin(),e.model.libraryClasses.end(),[](const auto& c) { return !c.reason.empty(); });
      for (auto [derived,base] : bases) if (reachable(derived)) mark(base,"base of a library-reachable class");
      for (auto pattern : unknownBases) if (reachable(pattern))
        for (const auto& [r,id] : classes) if (e.model.libraryClasses[id].user)
          mark(r,"unclassified dependent base of a reachable class");
      for (auto [a,b] : conversions) {
        if (reachable(a)) mark(b,"conversion to/from a library-reachable class");
        if (reachable(b)) mark(a,"conversion to/from a library-reachable class");
      }
      changed = before != std::count_if(e.model.libraryClasses.begin(),e.model.libraryClasses.end(),[](const auto& c) { return !c.reason.empty(); });
    } while (changed);
    for (auto [decl,id] : e.declarations) {
      if (const auto* t = dyn_cast<FunctionTemplateDecl>(decl)) decl = t->getTemplatedDecl();
      if (const auto* m = dyn_cast<CXXMethodDecl>(decl); m && e.model.families[id].memberOf != noFamily)
        e.model.families[id].memberClass = index(m->getParent());
    }
    // System classes are useful during closure, but reports count user classes.
    // Preserve stable indices; mark their labels for filtering at reporting time.
    e.model.libraryReachabilityComputed = e.model.libraryReachabilityFailure.empty();
  }
};
