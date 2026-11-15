// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include <string_view>

namespace {
int runTests(const std::vector<std::string>& argv) {
  std::string_view filter;
  if (argv.size() == 3 && argv[1] == "--filter") filter = argv[2];
  else if (argv.size() != 1) { std::cerr << "usage: distill_unit_tests [--filter substring]\n"; return 1; }
  test::helper = std::filesystem::absolute(distill::pathFromUtf8(argv[0])).parent_path() /
#ifdef _WIN32
      "distill_test_helper.exe";
#else
      "distill_test_helper";
#endif
  int passed = 0, failed = 0, skipped = 0;
  for (const auto& entry : test::cases()) {
    if (std::string_view(entry.name).find(filter) == std::string_view::npos) continue;
    const auto before = test::failures;
    try { entry.run(); }
    catch (const test::Skip& skip) { ++skipped; std::cout << "SKIP " << entry.name << ": " << skip.reason << '\n'; continue; }
    catch (const std::exception& e) { ++test::failures; std::cerr << entry.name << ": " << e.what() << '\n'; }
    catch (...) { ++test::failures; std::cerr << entry.name << ": unknown exception\n"; }
    if (test::failures == before) { ++passed; std::cout << "PASS " << entry.name << '\n'; }
    else { ++failed; std::cout << "FAIL " << entry.name << '\n'; }
  }
  std::cout << passed << " passed, " << failed << " failed, " << skipped << " skipped; " << test::failures << " failed checks\n";
  return test::failures ? 1 : 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) args.push_back(distill::pathToUtf8(std::filesystem::path(argv[i])));
  return runTests(args);
}
#else
int main(int argc, char** argv) { return runTests({argv, argv + argc}); }
#endif
