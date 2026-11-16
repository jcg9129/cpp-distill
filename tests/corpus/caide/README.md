<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# caide measurement corpus

Copied from the user's own GPL-3.0 fork of caide-cpp-inliner at
`/workspace/caide-cpp-inliner/tests/cases/*` on 2026-11-16 (81 case directories).
The upstream project is https://github.com/slycelote/caide-cpp-inliner.
These sources retain their original notices and are distributed under GPL-3.0;
see the repository's [LICENSE](../../../LICENSE). SHA256SUMS identifies the
byte-for-byte copied files; generated cmd.txt and main.cpp adapters are excluded.

This is measurement input only. No golden output or expected-result assertions
were imported; D4 ports those assertions. Error cases, unsupported options,
missing judge compilers and newer dialects can legitimately produce error/raw/flat
rows. Keep/force-remove/verification metadata is preserved for the later port but
is not interpreted by measurements. No corpus program is run by measurement.

The original layout has one or more numbered C++ source fragments per case.
For the four multi-fragment cases, a generated main.cpp includes them in numeric
order as one translation unit. All other cases use 1.cpp. Generated cmd.txt
starts with GNU C++20/O0, appends the original clangOptions.txt words (so its
explicit dialect wins), and replaces TEST_ROOT with the case directory `.`.
Forced includes remain unsupported by cpp-distill and are recorded as errors.
No flags or input source contents are silently repaired to obtain a smaller row.
