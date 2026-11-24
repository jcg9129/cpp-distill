#include <cstddef>
alignas(8) int x{}
#include <cstddef>
;
int main() { return x; }
