// SPDX-License-Identifier: GPL-3.0-or-later
// Included only by the test runner. There is no product hook or flag.
#pragma once

// Force a retained family (including its union component) out AFTER closure.
// The resulting invariant/build/run checks are independent probes: a build
// fixture may also fail the static invariant, which is deliberately recorded.
void checkerCase(const distill::PipelineResult& baseline, const std::string& victim,
                 const std::string& expected, const std::string& mutation, const std::string& witness,
                 const distill::CompileCommand& command, const distill::DistillOptions& options,
                 distill::ConditionEvaluator& evaluator, const distill::ProcessRunner& runner) {
  using namespace distill;
  check(baseline.artifact == "pruned" && baseline.pruningModel && baseline.retention,
        "checker requires successful enabled pruning\n" + baseline.report);
  const auto& model = *baseline.pruningModel;
  auto forced = *baseline.retention;
  std::set<FamilyId> victims;
  for (FamilyId id = 0; id < model.families.size(); ++id)
    if (model.families[id].label == victim) victims.insert(id);
  check(!victims.empty(),"checker victim not found: " + victim + "\n" + baseline.report);
  for (bool changed = true; changed;) {
    changed = false;
    for (auto [a,b] : model.unions) if (victims.contains(a) || victims.contains(b)) {
      changed |= victims.insert(a).second; changed |= victims.insert(b).second;
    }
  }
  for (auto id : victims) {
    check(forced.families[id].kept,"checker victim was not retained: " + victim);
    forced.families[id].kept = false;
  }
  auto bytes = take(renderSource(model,&forced)).provenance.text;
  if (mutation == "token") {
    const auto pos = bytes.find("return 0;"); check(pos != bytes.npos,"missing mutation token");
    bytes.erase(pos+7,1);
  } else if (mutation == "line") {
    const auto pos = bytes.rfind("#line "); check(pos != bytes.npos,"missing mutation marker");
    const auto start = pos+6, end = bytes.find(' ',start);
    bytes.replace(start,end-start,std::to_string(std::stoul(bytes.substr(start,end-start))+1));
  }
  const auto rendering = renderingInvariant(model,&forced,bytes);
  const auto names = staticNameInvariant(model,forced,bytes);
  std::cout << "  checker " << victim << ": rendering=" << (rendering.ok() ? "pass" : rendering.error().message)
            << "; static=" << (names.ok() ? "pass" : names.error().message) << '\n';
  if (expected == "rendering-invariant") { check(!rendering.ok(),"rendering fault escaped invariant"); return; }
  check(rendering.ok(),"forced removal unexpectedly violates rendering invariant");
  if (expected == "static-invariant") {
    check(!names.ok() && names.error().message.find("static name invariant: removed name ") != std::string::npos,
          "macro-body name escaped static invariant");
  } else if (expected != "build") check(names.ok(),"expected checker limit violates static invariant");

  bool injected = false, buildFailed = false;
  ProcessRunner forcedRunner = [&](const ProcessSpec& spec) -> Result<ProcessResult> {
    for (const auto& arg : spec.argv) {
      const auto path = pathFromUtf8(arg);
      if (path.filename() != "pruned.cpp") continue;
      // Compile the forced bytes, restore the checked file immediately, and
      // let the pipeline's sample API compare the actual resulting executable.
      // Restoring isolates the runtime probe from checkedBytes' tamper guard.
      const auto saved = read(path); write(path,bytes); injected = true;
      auto process = runner(spec); write(path,saved);
      if (process.ok()) {
        const auto& r = process.value();
        check(!r.timedOut && !r.signal && !r.stdoutTruncated && !r.stderrTruncated,"forced checker process limit");
        buildFailed = r.exitCode != 0;
      }
      return process;
    }
    return runner(spec);
  };
  auto result = take(distillProgram(command,options,evaluator,forcedRunner));
  check(injected,"checker did not compile forced candidate\n" + result.report);
  if (expected == "build") {
    check(buildFailed && result.artifact == "flat" && result.diagnostic.find("pruned checks: process") != std::string::npos,
          "called-function removal escaped build\n" + result.report + result.diagnostic);
  } else {
    check(!buildFailed,"forced checker candidate must build\n" + result.report + result.diagnostic);
    if (expected == "run")
      check(result.artifact == "flat" && result.diagnostic.find("differs on sample") != std::string::npos,
            "constructor side effect escaped sample comparison\n" + result.report + result.diagnostic);
    else {
      check(expected == "none" || expected == "static-invariant","unknown checker: " + expected);
      check(result.artifact == "pruned" && result.runtime == "compared 1 samples",
            "documented build/run limit did not reproduce\n" + result.report + result.diagnostic);
    }
  }
  if (!witness.empty()) {
    const auto input = command.cwd / "witness.in"; write(input,witness);
    auto o = options; o.samples = {{"witness",input}};
    auto witnessed = take(distillProgram(command,o,evaluator,forcedRunner));
    check(witnessed.artifact == "flat" && witnessed.diagnostic.find("differs on sample witness") != std::string::npos,
          "branch-limit witness did not distinguish the removed helper\n" + witnessed.report + witnessed.diagnostic);
  }
}
