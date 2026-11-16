// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pipeline/pipeline.h"
#include "core/path.h"
#include "core/temporary.h"
#include "evaluate.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
using namespace distill;
template<class T> T take(Result<T> r) { if (!r.ok()) throw Error{r.error().message}; return std::move(r.value()); }
std::string read(const std::filesystem::path& path) {
  std::ifstream in(path,std::ios::binary); if (!in) throw Error{"cannot read " + pathToUtf8(path)};
  return {std::istreambuf_iterator<char>(in),{}};
}
std::string cell(std::string s) {
  for (auto& c : s) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
  std::string escaped = "\"";
  for (const auto c : s) { if (c == '"') escaped += '"'; escaped += c; }
  return escaped + '"';
}
std::string primaryRule(const PruningResult& retention, FamilyId id) {
  for (std::size_t visited = 0; visited < retention.families.size(); ++visited) {
    const auto& f = retention.families.at(id);
    if (f.reason == "family aggregation" && f.cause != noFamily) { id = f.cause; continue; }
    if (f.reason.starts_with("root:") || f.reason.starts_with("pinned:") || f.reason.starts_with("never removed:")) return "1 roots";
    if (f.reason.starts_with("name:")) return "2 name";
    if (f.reason.starts_with("associated with")) return "3 association";
    if (f.reason.starts_with("AST:")) return "4 AST";
    if (f.reason.starts_with("dependent name:")) return "5 dependent name";
    return f.reason; // Disabled analysis is labelled, never counted as a rule.
  }
  throw Error{"cyclic family attribution"};
}
// cmd.txt uses the same no-expansion shell words as the case runners.
std::vector<std::string> words(std::string_view text) {
  std::vector<std::string> out; std::string word; char quote = 0; bool started = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto c = text[i];
    if (c == '\\' && quote != '\'') { if (++i == text.size()) throw Error{"dangling escape"}; word += text[i]; started = true; }
    else if (quote) { if (c == quote) quote = 0; else word += c; }
    else if (c == '\'' || c == '"') { quote = c; started = true; }
    else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (started) { out.push_back(word); word.clear(); started = false; }
    } else { word += c; started = true; }
  }
  if (quote) throw Error{"unclosed quote"};
  if (started) out.push_back(word);
  return out;
}
int run(int argc, char** argv) {
  if (argc != 5 || std::string_view(argv[1]) != "--measure")
    throw Error{"usage: distill_measure --measure <worker> <source-or-directory> <table.tsv>"};
  const auto gcc = testing::testGcc(); if (!gcc) throw Error{"measurement requires GCC"};
  const auto input = std::filesystem::absolute(pathFromUtf8(argv[3]));
  std::vector<std::filesystem::path> files;
  if (std::filesystem::is_regular_file(input)) files.push_back(input);
  else for (const auto& e : std::filesystem::recursive_directory_iterator(input))
    if (e.is_regular_file() && (e.path().filename() == "main.cpp" ||
        (e.path().filename() == "1.cpp" && !std::filesystem::exists(e.path().parent_path()/"main.cpp")))) files.push_back(e.path());
  std::sort(files.begin(),files.end());
  if (files.empty()) throw Error{"no solutions in measurement directory"};
  std::ofstream table(pathFromUtf8(argv[4]),std::ios::binary);
  if (!table) throw Error{"cannot write measurement table"};
  table << "file\tresult\traw_bytes\tflat_bytes\tpruned_bytes\tkept_bytes_primary\tpruning_disable_reasons\tdiagnostic\n";
  TemporaryDirectory work(".measure-",std::filesystem::current_path());
  auto evaluator = testing::testEvaluator(work.path);
  bool infrastructureFailure = false;
  for (const auto& file : files) {
    std::map<std::string,std::size_t> sizes;
    std::string result = "error", primary, disabled, diagnostic;
    try {
      std::vector<std::string> args{pathToUtf8(*gcc),"-std=gnu++20","-O0",pathToUtf8(file)};
      const auto cmdFile = file.parent_path()/"cmd.txt";
      if (std::filesystem::exists(cmdFile)) {
        std::istringstream in(read(cmdFile)); std::string line, command;
        while (std::getline(in,line)) if (!line.starts_with('#')) command += line+'\n';
        args = words(command); if (args.empty()) throw Error{"empty cmd.txt"}; args[0] = pathToUtf8(*gcc);
      }
      auto command = take(parseCompileCommand(args,file.parent_path()));
      DistillOptions options; options.pruningWorker = std::filesystem::absolute(pathFromUtf8(argv[2]));
      options.temporaryRoot = work.path;
      auto r = take(distillProgram(command,options,*evaluator,[&](const ProcessSpec& spec) {
        for (const auto& arg : spec.argv) {
          const auto path = pathFromUtf8(arg); const auto name = pathToUtf8(path.filename());
          if (name == "raw.cpp" || name == "flat.cpp" || name == "pruned.cpp") sizes[name] = read(path).size();
        }
        auto process = runProcess(spec);
        // Keep every failing command's complete bounded diagnostics, even when
        // the pipeline recovers to flat/raw or the corpus intentionally fails.
        if (!process.ok()) std::cerr << file << ": " << process.error().message << '\n';
        else if (process.value().exitCode || process.value().signal || process.value().timedOut || !process.value().stderrText.empty()) {
          std::cerr << file << ": cwd=" << spec.cwd << " argv=";
          for (const auto& arg : spec.argv) std::cerr << ' ' << std::quoted(arg);
          std::cerr << "\nexit=" << process.value().exitCode << " timeout=" << process.value().timedOut
                    << "\n" << process.value().stderrText << '\n';
        }
        return process;
      }));
      result = r.exitCode == 1 ? "error" : r.artifact + "/" + r.verification;
      diagnostic = r.diagnostic;
      if (r.retention) {
        if (r.pruningModel) {
          std::map<std::string,std::size_t> rules;
          for (FamilyId id = 0; id < r.retention->families.size(); ++id) if (r.retention->families[id].kept) {
            const auto rule = primaryRule(*r.retention,id);
            for (auto range : r.pruningModel->families[id].ranges) rules[rule] += range.end-range.begin;
          }
          for (const auto& [rule,bytes] : rules) primary += (primary.empty() ? "" : "; ") + rule + "=" + std::to_string(bytes);
          primary += (primary.empty() ? "" : "; ") + std::string("structural=") +
                     std::to_string(r.pruningModel->text.size()-r.retention->keptBytes-r.retention->removedBytes);
        }
        for (const auto& reason : r.retention->disabledReasons) disabled += (disabled.empty() ? "" : "; ") + reason;
        for (const auto& reason : r.retention->disabledReasons)
          if (reason.starts_with("pruning worker") || reason.starts_with("invalid pruning worker")) infrastructureFailure = true;
      } else disabled = "analysis not reached";
      if (r.keptWork) std::filesystem::remove_all(*r.keptWork);
      if (result == "error" || result.starts_with("flat/") || result.starts_with("raw/"))
        std::cerr << file << ": " << result << '\n' << r.report << r.diagnostic << '\n';
    } catch (const Error& e) { diagnostic = e.message; std::cerr << file << ": " << e.message << '\n'; }
    catch (const std::exception& e) { diagnostic = e.what(); std::cerr << file << ": " << e.what() << '\n'; }
    auto size = [&](const char* key) { return sizes.contains(key) ? std::to_string(sizes.at(key)) : "-"; };
    table << cell(pathToUtf8(std::filesystem::relative(file,std::filesystem::current_path()))) << '\t' << result << '\t'
          << size("raw.cpp") << '\t' << size("flat.cpp") << '\t' << size("pruned.cpp") << '\t'
          << cell(primary) << '\t' << cell(disabled) << '\t' << cell(diagnostic) << '\n';
    table.flush(); if (!table) throw Error{"measurement write failed"};
  }
  return infrastructureFailure ? 1 : 0;
}
}
int main(int argc, char** argv) {
  try { return run(argc,argv); }
  catch (const Error& e) { std::cerr << e.message << '\n'; return 1; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
