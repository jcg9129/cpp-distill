// SPDX-License-Identifier: GPL-3.0-or-later
// LLVM-free CI entry point; the released CLI uses the same command handler.
#include "cli/judge.h"
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout),_O_BINARY); _setmode(_fileno(stderr),_O_BINARY);
#endif
  try {
    auto options = distill::parseDistillOptions({argv+1,argv+argc});
    if (!options.ok()) throw options.error();
    if (options.value().help) { std::cout << distill::distillHelp(); return 0; }
    if (options.value().version) { std::cout << distill::distillVersion(); return 0; }
    if (!options.value().judgeProbe && !options.value().judgeCheck) throw distill::Error{"profile CI executable supports judge diagnostics only"};
    return distill::runJudgeDiagnostic(options.value());
  } catch (const distill::Error& e) { std::cerr << e.message << '\n'; return 1; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
