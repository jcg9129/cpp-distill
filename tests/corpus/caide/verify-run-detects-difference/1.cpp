#include <cstdio>

struct Greeting {
    Greeting() { std::puts("hello from the constructor"); }
};

Greeting greeting;

int main() {
    std::puts("main");
}
