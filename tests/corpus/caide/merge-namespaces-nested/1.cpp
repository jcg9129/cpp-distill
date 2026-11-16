// Regression test: consecutive C++17 nested-namespace definitions
// ("namespace a::b { ... }") must not crash the namespace-merging pass and
// must be left unmerged (the pass only handles a plain "namespace <name> {"
// prefix). A simple same-named pair is included to confirm merging still works.
namespace simple {
    void used1() {}
}

namespace simple {
    void used2() {}
}

namespace a::b {
    void used3() {}
}

namespace a::b {
    void used4() {}
}

int main() {
    simple::used1();
    simple::used2();
    a::b::used3();
    a::b::used4();
}
