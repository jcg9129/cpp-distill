#include <type_traits>

struct FieldOnly {
    unsigned char bytes[9];
};

int defaultValue() { return 1; }
int copyValue() { return 2; }
int moveValue() { return 3; }
int assignValue() { return 4; }

struct Layout {
    int a, b;
    FieldOnly field;
    unsigned : 0;
    unsigned bits : 3;
    unsigned : 5;
    union {
        int word;
        unsigned char bytes[13];
    };
    struct {
        unsigned char reserved[17];
    };
    Layout() : a(defaultValue()) {}
    Layout(const Layout&) : a(copyValue()) {}
    Layout(Layout&&) : a(moveValue()) {}
    Layout& operator=(const Layout&) { a = assignValue(); return *this; }
    Layout& operator=(Layout&&) = delete;
    ~Layout() = default;
    void unused() {}
};

template<class T>
struct Pattern {
    T value, reserved;
    Pattern() = default;
    Pattern(const Pattern&) = delete;
};

template<class T>
struct Pattern<T*> {
    T* value;
    unsigned char reserved[19];
    Pattern() = delete;
};

template<>
struct Pattern<char> {
    unsigned char reserved[23];
    Pattern() = default;
    Pattern& operator=(const Pattern&) = delete;
};

int main() {
    return sizeof(Layout) < 48 || sizeof(Pattern<int>) != 2 * sizeof(int) ||
        sizeof(Pattern<int*>) < 19 || sizeof(Pattern<char>) != 23 ||
        std::is_trivially_default_constructible<Layout>::value ||
        std::is_copy_constructible<Pattern<int>>::value ||
        std::is_default_constructible<Pattern<int*>>::value ||
        std::is_copy_assignable<Pattern<char>>::value;
}
