// SPDX-License-Identifier: GPL-3.0-or-later
struct Mint { int x; int value() const { return x; } };
Mint operator+(Mint a, Mint b) { return {a.x + b.x}; }
Mint operator*(Mint a, Mint b) { return {a.x * b.x}; }
int main() { return 0; }
