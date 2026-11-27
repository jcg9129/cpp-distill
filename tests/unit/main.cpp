// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include <string_view>

namespace {
int runTests(const std::vector<std::string>& argv) {
  std::string_view filter;
  std::vector<std::string_view> excluded;
  for (std::size_t i = 1; i < argv.size(); ++i) {
    if ((argv[i] != "--filter" && argv[i] != "--exclude-filter") || i + 1 == argv.size()) {
      std::cerr << "usage: distill_unit_tests [--filter substring] [--exclude-filter substring]...\n";
      return 1;
    }
    if (argv[i] == "--filter") filter = argv[++i];
    else excluded.push_back(argv[++i]);
  }
  test::helper = std::filesystem::absolute(distill::pathFromUtf8(argv[0])).parent_path() /
#ifdef _WIN32
      "distill_test_helper.exe";
#else
      "distill_test_helper";
#endif
  int passed = 0, failed = 0, skipped = 0;
  for (const auto& entry : test::cases()) {
    if (std::string_view(entry.name).find(filter) == std::string_view::npos) continue;
    bool exclude = false;
    for (const auto substring : excluded)
      exclude |= std::string_view(entry.name).find(substring) != std::string_view::npos;
    if (exclude) continue;
    std::cout << "RUN  " << entry.name << std::endl;
    const auto before = test::failures;
    try { entry.run(); }
    catch (const test::Skip& skip) { ++skipped; std::cout << "SKIP " << entry.name << ": " << skip.reason << std::endl; continue; }
    catch (const std::exception& e) { ++test::failures; std::cerr << entry.name << ": " << e.what() << '\n'; }
    catch (...) { ++test::failures; std::cerr << entry.name << ": unknown exception\n"; }
    if (test::failures == before) { ++passed; std::cout << "PASS " << entry.name << std::endl; }
    else { ++failed; std::cout << "FAIL " << entry.name << std::endl; }
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
