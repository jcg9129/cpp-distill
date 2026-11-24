#include <vector> // first vector
#include <type_traits> // first traits
#include <vector> // later vector
#include <type_traits> // later traits
#include <cstdio>
#include <stdio.h>
int main() { std::vector<int> v{7}; std::printf("%d\n", v[0]); }
