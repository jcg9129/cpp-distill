// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace fs = std::filesystem;

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: embed-resources <include directory> <output directory>\n";
    return 2;
  }
  try {
    const fs::path root = argv[1], output = argv[2];
    std::vector<fs::path> paths;
    for (const auto &entry : fs::recursive_directory_iterator(root))
      if (entry.is_regular_file())
        paths.push_back(entry.path().lexically_relative(root));
    std::sort(paths.begin(), paths.end(), [](const auto &a, const auto &b) {
      return a.generic_string() < b.generic_string();
    });
    if (paths.empty())
      throw std::runtime_error("resource directory is empty");
    fs::create_directories(output);
    std::ofstream table(output / "table.cpp");
    table.exceptions(std::ios::badbit | std::ios::failbit);
    table << "// SPDX-License-Identifier: GPL-3.0-or-later\n"
             "#include \"resources.h\"\nnamespace distill {\n";
    std::vector<std::size_t> sizes;
    for (std::size_t i = 0; i < paths.size(); ++i) {
      std::ifstream input(root / paths[i], std::ios::binary);
      if (!input)
        throw std::runtime_error("cannot read " + paths[i].string());
      std::ofstream source(output / ("resource_" + std::to_string(i) + ".cpp"));
      source.exceptions(std::ios::badbit | std::ios::failbit);
      source << "// SPDX-License-Identifier: GPL-3.0-or-later\nnamespace distill {\n"
                "extern const unsigned char resource_" << i << "[] = {\n";
      std::size_t size = 0;
      char byte;
      while (input.get(byte)) {
        source << static_cast<unsigned>(static_cast<unsigned char>(byte)) << ',';
        if (++size % 32 == 0)
          source << '\n';
      }
      if (!input.eof())
        throw std::runtime_error("resource read failed");
      // LLVM/Clang buffers require a NUL sentinel beyond the logical file size.
      source << '0';
      source << "\n};\n}\n";
      source.close();
      sizes.push_back(size);
      table << "extern const unsigned char resource_" << i << "[];\n";
    }
    table << "static const Resource entries[] = {\n";
    for (std::size_t i = 0; i < paths.size(); ++i)
      table << '{' << std::quoted(paths[i].generic_string()) << ", resource_" << i
            << ", " << sizes[i] << "},\n";
    table << "};\nstd::span<const Resource> resources() { return entries; }\n}\n";
    table.close();
  } catch (const std::exception &error) {
    std::cerr << "embed-resources: " << error.what() << '\n';
    return 1;
  }
}
