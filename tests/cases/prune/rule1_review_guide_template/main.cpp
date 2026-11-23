// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T>
struct Box { Box(T) {} };
template<class T>
Box(T) -> Box<long>;
template<class T>
Box(T*) -> Box<char>;
int main() {
    Box b(1);
    int result = !__is_same(decltype(b), Box<long>);
    std::printf("%d\n", result);
    return result;
}
