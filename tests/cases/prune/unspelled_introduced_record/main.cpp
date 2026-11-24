// SPDX-License-Identifier: GPL-3.0-or-later
// R is declared only here: retaining p needs this shared declaration.
struct R* helper();
R* p = nullptr;
int main() { return p != nullptr; }
