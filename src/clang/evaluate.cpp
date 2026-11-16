// SPDX-License-Identifier: GPL-3.0-or-later
#include "clang/evaluate.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>

namespace distill {
Result<bool> ClangEvaluator::evaluate(const ConditionInput& input) {
  // A pure in-memory cc1 invocation: no driver, real FS overlay, resource
  // directory, header search or startup headers. #if needs no resource headers.
  const auto model = input.requirements.integers.value_or(IntegerModel{});
  auto triple = evaluationTriple(model);
  if (!triple.ok()) return triple.error();
  auto memory = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  auto invocation = std::make_shared<clang::CompilerInvocation>();
  auto diagnostics = clang::CompilerInstance::createDiagnostics(*memory, invocation->getDiagnosticOpts(),
      new clang::IgnoringDiagConsumer, true);
  if (!diagnostics) return Error{"cannot create Clang diagnostics"};
  std::vector<std::string> args{"-x", "c++", "-std=" + input.requirements.dialect.value_or("gnu++20"),
      "-triple", triple.value(), "-undef", "-nostdsysteminc", "-nobuiltininc", "-ffreestanding"};
  if (input.requirements.integers && !input.requirements.integers->charSigned) args.push_back("-fno-signed-char");
  args.push_back("condition.cpp");
  std::vector<const char*> argv;
  for (const auto& arg : args) argv.push_back(arg.c_str());
  if (!clang::CompilerInvocation::CreateFromArgs(*invocation, argv, *diagnostics)) return Error{"invalid Clang evaluation requirements"};
  invocation->getPreprocessorOpts().UsePredefines = false;
  invocation->getLangOpts().CharIsSigned = model.charSigned;
  invocation->getLangOpts().WCharSize = model.wcharBits / model.charBits;
  invocation->getLangOpts().WCharIsSigned = model.wcharSigned;
  clang::CompilerInstance compiler(invocation);
  compiler.setVirtualFileSystem(memory);
  compiler.setDiagnostics(diagnostics);
  compiler.createFileManager();
  compiler.createSourceManager();
  if (!compiler.createTarget()) return Error{"cannot create Clang target"};
  const auto& target = compiler.getTarget();
  if (target.getCharWidth() != model.charBits || target.getIntWidth() != model.intBits ||
      target.getLongWidth() != model.longBits || target.getLongLongWidth() != model.longLongBits ||
      target.getIntMaxTWidth() != model.intmaxBits || target.getWCharWidth() != model.wcharBits ||
      clang::TargetInfo::isTypeSigned(target.getWCharType()) != model.wcharSigned)
    return Error{"Clang target does not represent requested integer model"};
  compiler.createPreprocessor(clang::TU_Complete);
  auto& pp = compiler.getPreprocessor();
  // -undef/UsePredefines do not unregister intrinsic operators. Enumerate the
  // actual Clang instance too, so no Clang-only binding can leak into defined().
  std::string synthetic;
  for (const auto& entry : pp.macros(false)) synthetic += "#undef " + entry.first->getName().str() + '\n';
  synthetic += isolatedConditionSource(input);
  auto& sources = compiler.getSourceManager();
  sources.setMainFileID(sources.createFileID(llvm::MemoryBuffer::getMemBufferCopy(synthetic, "condition.cpp")));
  diagnostics->getClient()->BeginSourceFile(compiler.getLangOpts(), &pp);
  pp.EnterMainSourceFile();
  bool yes = false, no = false;
  clang::Token token;
  do {
    pp.Lex(token);
    if (token.is(clang::tok::identifier)) {
      const auto name = pp.getSpelling(token);
      yes |= name == input.trueToken;
      no |= name == input.falseToken;
    }
  } while (!token.is(clang::tok::eof));
  diagnostics->getClient()->EndSourceFile();
  if (diagnostics->hasErrorOccurred() || yes == no) return Error{"Clang preprocessing error"};
  return yes;
}
} // namespace distill
