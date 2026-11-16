// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/command/options.h"
#include "core/path.h"

namespace distill {
Result<CliOptions> parseDistillOptions(const std::vector<std::string>& args) {
  CliOptions o;
  bool noPrune = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& a = args[i];
    if (a == "--help") { o.help = true; return o; }
    if (a == "--no-prune") noPrune = true;
    else if (a == "--strict") o.strict = true;
    else if (a == "-v") o.verbose = true;
    else if (a == "--keep" || a == "--verify-run" || a == "--strip-comments") return Error{a + ": not supported yet"};
    else if (a == "-o" || a == "--judge" || a == "--report") {
      if (++i == args.size() || args[i].empty()) return Error{"missing value for " + a};
      (a == "-o" ? o.output : a == "--judge" ? o.judge : o.report) = args[i];
    } else if (a.starts_with('-')) return Error{"unknown distill option: " + a + " (compiler options must follow the compiler name)"};
    else { o.compile.assign(args.begin() + i, args.end()); break; }
  }
  if (!noPrune) return Error{"--no-prune is required; pruning arrives in D3"};
  if (o.compile.empty()) return Error{"missing compile command or source file"};
  const auto ext = pathToUtf8(pathFromUtf8(o.compile.front()).extension());
  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++" || ext == ".C") {
    o.compile.insert(o.compile.begin(), "g++"); o.compilerExplicit = false;
  }
  return o;
}
std::string distillHelp() {
  return "Usage: distill [distill options] [<compile command>]\n"
      "  -o <file>          output (default stdout)\n"
      "  --judge <name|file> judge profile\n"
      "  --report <file>    validation and conditional report\n"
      "  --no-prune         required in D2; pruning arrives in D3\n"
      "  --strict           exit 3 unless pruned with target verification\n"
      "  -v                 progress on stderr\n"
      "  --help             show this help\n"
      "  --keep, --verify-run, --strip-comments: not supported yet\n"
      "distill --no-prune main.cpp means distill --no-prune g++ main.cpp\n";
}
} // namespace distill
