// SPDX-License-Identifier: GPL-3.0-or-later
#include "clang/prune_extract.h"
#include "core/path.h"
#include <exception>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace {
int fail(const std::string& message) {
  std::cerr << "pruning worker: " << message << '\n';
  return 1;
}
int run(const std::vector<std::string>& args) try {
  if (args.size() != 3 || args[1] != "--internal-prune") return fail("expected --internal-prune <input>");
  std::ifstream f(distill::pathFromUtf8(args[2]),std::ios::binary);
  if (!f) return fail("cannot open input: " + args[2]);
  std::string bytes;
  char buffer[16384];
  while (f.read(buffer,sizeof(buffer)) || f.gcount()) {
    if (bytes.size() + static_cast<std::size_t>(f.gcount()) > 256 * 1024 * 1024) return fail("input exceeds 256 MiB protocol byte limit");
    bytes.append(buffer,static_cast<std::size_t>(f.gcount()));
  }
  if (!f.eof()) return fail("cannot read input: " + args[2]);
  auto input = distill::parsePruningModel(bytes); if (!input.ok()) return fail("decode input: " + input.error().message);
  auto extracted = distill::extractPruningInWorker(std::move(input.value())); if (!extracted.ok()) return fail("extract: " + extracted.error().message);
  auto result = distill::serializePruningModel(extracted.value()); if (!result.ok()) return fail("encode result: " + result.error().message);
  std::cout << result.value(); return std::cout ? 0 : fail("cannot write result");
} catch (const distill::Error& e) { return fail(e.message); }
catch (const std::exception& e) { return fail(e.what()); }
catch (...) { return fail("unknown exception"); }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  _setmode(_fileno(stdout),_O_BINARY); _setmode(_fileno(stderr),_O_BINARY);
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) args.push_back(distill::pathToUtf8(std::filesystem::path(argv[i])));
  return run(args);
}
#else
int main(int argc, char** argv) { return run({argv,argv+argc}); }
#endif
