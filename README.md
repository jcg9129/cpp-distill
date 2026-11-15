<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# cpp-distill

`distill` will inline a competitive-programming solution's own C++ headers into one `.cpp` and prune unused code using Clang's full AST. The intended distribution is a self-contained executable for Linux x86_64/arm64, macOS arm64/x86_64, and Windows x86_64, with Clang resource headers embedded and system headers supplied by the user's GCC.

Status: **in design; D1 spike**. See [the distribution spike](docs/d1-spike.md) for workflows, measurements, and limitations. The inliner and pruning engine are not implemented yet.

License: GPL-3.0-or-later; see [LICENSE](LICENSE).

cpp-distill is a from-scratch successor of caide-cpp-inliner by slycelote (https://github.com/slycelote/caide-cpp-inliner).
