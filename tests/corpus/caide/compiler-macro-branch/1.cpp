// The inliner preprocesses with Clang, but the judge compiles with g++.
// Both branches must survive, or g++ gets the Clang branch (returns 1, not 2).
#ifdef __clang__
constexpr int backend = 1;
#else
constexpr int backend = 2;
#endif

int main() {
    return backend;
}
