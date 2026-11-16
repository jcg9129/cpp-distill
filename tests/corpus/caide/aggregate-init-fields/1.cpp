#include <cstdint>

// Aggregate-initialized and only partially read (.c): the base visitor records
// no MemberExpr for the positionally-written fields, so a, b would be pruned and
// the surviving `return {1, 2, 3}` would no longer match the struct. All three
// fields must survive together.
struct point3 {
    std::uint32_t a, b, c;
};

// Never used anywhere: must still be removed.
struct unused_pair {
    std::uint32_t x, y;
};

static point3 make() {
    return {1, 2, 3};
}

int main() {
    return int(make().c);
}
