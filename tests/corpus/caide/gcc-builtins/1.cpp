#include <cstdio>
int deadPopcount(unsigned x) { return __builtin_popcount(x); }
int countBits(unsigned x) { return __builtin_popcount(x); }
int main() { return countBits(255u); }
