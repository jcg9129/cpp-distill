// SPDX-License-Identifier: GPL-3.0-or-later
#include "clang/prune_extract.h"
#include "core/path.h"
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace {
int run(const std::vector<std::string>& args) {
  if (args.size() != 3 || args[1] != "--internal-prune") return 1;
  std::ifstream f(distill::pathFromUtf8(args[2]),std::ios::binary);
  std::string bytes;
  char buffer[16384];
  while (f.read(buffer,sizeof(buffer)) || f.gcount()) {
    if (bytes.size() + static_cast<std::size_t>(f.gcount()) > 256 * 1024 * 1024) return 1;
    bytes.append(buffer,static_cast<std::size_t>(f.gcount()));
  }
  if (!f.eof()) return 1;
  auto input = distill::parsePruningModel(bytes); if (!input.ok()) return 1;
  auto extracted = distill::extractPruningInWorker(std::move(input.value())); if (!extracted.ok()) return 1;
  auto result = distill::serializePruningModel(extracted.value()); if (!result.ok()) return 1;
  std::cout << result.value(); return std::cout ? 0 : 1;
}
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
