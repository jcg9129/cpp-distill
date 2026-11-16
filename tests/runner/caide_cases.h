// SPDX-License-Identifier: GPL-3.0-or-later
// Included by pipeline_cases.cpp: shares its command/metadata and file helpers.
std::vector<std::string> caseTokens(std::string_view text) {
  std::vector<std::string> out;
  for (const auto& line : take(scanDirectives(text)).lines)
    for (const auto& token : line.tokens) out.push_back(token.text);
  return out;
}
bool containsTokens(const std::vector<std::string>& tokens, std::string_view text) {
  const auto snippet = caseTokens(text);
  check(!snippet.empty(),"empty token assertion: " + std::string(text));
  return std::search(tokens.begin(),tokens.end(),snippet.begin(),snippet.end()) != tokens.end();
}
int caideAssertionModels() {
  const auto tokens = caseTokens("int kept; /* int removed; */\nconst char* text = \" int removed; \";\nlong helper (int); int unusedLong;\n");
  check(containsTokens(tokens,"int kept;"),"kept token sequence not found");
  check(containsTokens(tokens,"long helper(int)"),"formatting changed token assertion");
  check(!containsTokens(tokens,"int removed;"),"comment/literal satisfied declaration assertion");
  check(!containsTokens(tokens,"unused"),"partial identifier satisfied name assertion");
  std::cout << "PASS caide token assertions\n"; return 0;
}
int caideCase(const std::filesystem::path& dir, const std::filesystem::path& worker,
              const std::filesystem::path& gcc) {
  Temp temp;
  const auto cwd = temp.path / "input";
  std::filesystem::create_directory(cwd);
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    const auto target = cwd / entry.path().lexically_relative(dir);
    if (entry.is_directory()) std::filesystem::create_directory(target);
    else write(target,read(entry.path()));
  }
  const auto expected = metadata(read(cwd / "expect.txt"));
  auto value = [&](const char* key) { auto i = expected.find(key); return i == expected.end() ? std::string{} : i->second; };
  std::string diagnostics, report, bytes;
  KeptWork kept;
  int code = 1;
  try {
    auto args = words(uncomment(read(cwd / "cmd.txt"))); args.at(0) = pathToUtf8(gcc);
    auto command = parseCompileCommand(args,cwd);
    if (!command.ok()) diagnostics = command.error().message;
    else {
      auto evaluator = testing::testEvaluator(temp.path);
      DistillOptions o; o.pruningWorker = worker; o.temporaryRoot = temp.path;
      o.noPrune = value("prune") == "no"; o.stripComments = value("strip") == "yes";
      if (!value("keep").empty()) o.keep = words(value("keep"));
      std::vector<std::string> samples;
      for (const auto& [key,text] : expected) if (key.starts_with("sample.")) samples.push_back(text);
      o.samples = take(collectRunSamples(samples,cwd));
      auto r = take(distillProgram(command.value(),o,*evaluator,[&](const ProcessSpec& spec) {
        auto process = runProcess(spec);
        // Full bounded diagnostics, including commands, for every SDK failure.
        std::ostringstream detail; detail << "cwd=" << spec.cwd << " argv=";
        for (const auto& arg : spec.argv) detail << ' ' << std::quoted(arg);
        if (process.ok()) {
          const auto& r = process.value();
          detail << "\nexit=" << r.exitCode << " signal=" << r.signal.value_or(0)
                 << " timeout=" << r.timedOut << " stdout_truncated=" << r.stdoutTruncated
                 << " stderr_truncated=" << r.stderrTruncated << '\n' << r.stderrText;
          if (spec.argv.size() == 1) detail << "\nstdout:\n" << r.stdoutText;
        }
        else detail << '\n' << process.error().message;
        diagnostics += detail.str() + '\n';
        return process;
      }));
      code = r.exitCode; report = r.report; diagnostics += r.diagnostic; bytes = r.bytes;
      if (r.keptWork) kept.path = *r.keptWork;
      // Only specifically documented missing compiler features permit a baseline
      // error; later worker/check failures can never take this alternative.
      bool missingFeature = false;
      for (const auto& [key,text] : expected) if (key == "baseline-feature" || key.starts_with("baseline-feature."))
        missingFeature |= r.diagnostic.find(text) != r.diagnostic.npos;
      const bool missingDialect = missingFeature && r.checks.empty() &&
          r.diagnostic.find("unrecognized command-line option") != r.diagnostic.npos &&
          r.diagnostic.find("-std=c++2c") != r.diagnostic.npos;
      if (code == 1 && missingFeature && (r.diagnostic.starts_with("baseline failed:") || missingDialect)) {
        check(bytes.empty(),"baseline failure returned bytes");
        std::cout << "PASS " << dir.filename() << ": documented unavailable baseline feature\n" << report << diagnostics;
        return 0;
      }
      if (r.retention) for (const auto& why : r.retention->disabledReasons)
        check(!why.starts_with("pruning worker") && !why.starts_with("invalid pruning worker") &&
              !why.starts_with("Clang parse error") && !why.starts_with("invalid Clang parse configuration") &&
              !why.starts_with("unsupported Clang target"),"SDK extraction failed: " + why);
      if (code != 1) check(r.artifact == value("artifact"),"wrong artifact: " + r.artifact);
      if (!value("runtime").empty()) check(r.runtime == value("runtime"),"wrong runtime: " + r.runtime);
    }
    check(code == std::stoi(value("exit")),"wrong exit: " + std::to_string(code));
    if (code == 1) check(bytes.empty(),"failed case returned bytes");
    const auto tokens = caseTokens(bytes);
    for (const auto& [key,text] : expected) {
      if (key == "reason" || key.starts_with("report."))
        check((report+diagnostics).find(text) != std::string::npos,"missing reason/report: " + text);
      if (key == "contains" || key.starts_with("contains."))
        check(containsTokens(tokens,text),"missing tokens: " + text);
      if (key == "absent" || key.starts_with("absent."))
        check(!containsTokens(tokens,text),"unexpected tokens: " + text);
    }
    if (value("strip") == "yes") check(take(scanDirectives(bytes)).comments.empty(),"comments survived stripping");
    std::cout << "PASS " << dir.filename() << '\n'; return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << dir.filename() << ": " << e.what() << '\n' << report << diagnostics << "\nOUTPUT:\n" << bytes;
    return 1;
  }
}
