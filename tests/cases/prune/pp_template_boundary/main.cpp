#include <cstddef>
// distill: keep
template<class T> void f() {}
#include <cstddef>
int main() { f<int>(); }
