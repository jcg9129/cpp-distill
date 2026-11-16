// Regression: a nested-namespace definition ("namespace a::b { ... }") shares its
// outer namespace (a) with a following plain "namespace a { ... }". They must NOT
// be merged -- removing the nested block's shared closing brace would pull the
// plain block's contents inside a::b, corrupting the namespace structure.
namespace a::b {
    int inB() { return 1; }
}

namespace a {
    int inA() { return 2; }
}

int main() {
    return a::b::inB() + a::inA();
}
