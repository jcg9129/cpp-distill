#include <cstddef>
#include <ranges>
#include <type_traits>
#include <vector>

namespace lib {
    template<typename R>
    struct rank : std::integral_constant<std::size_t, 0> {
        using scalar = R;
    };

    // Partial specialization selected only in the dependent constraint below.
    template<std::ranges::range R>
    struct rank<R> : std::integral_constant<std::size_t, 1 + rank<std::ranges::range_value_t<R>>::value> {
        using scalar = typename rank<std::ranges::range_value_t<R>>::scalar;
    };

    template<typename R>
    inline constexpr std::size_t rank_v = rank<std::remove_cvref_t<R>>::value;

    // `scalar` is reached only through this dependent member alias.
    template<typename R>
    using scalar_t = typename rank<std::remove_cvref_t<R>>::scalar;

    template<std::ranges::sized_range Range>
        requires(rank_v<Range> == 1)
    scalar_t<Range> first_scalar(Range &&r) {
        return *std::ranges::begin(r);
    }
}

int main() {
    std::vector<int> v{7, 8, 9};
    return int(lib::first_scalar(v));   // expect 7; the partial spec and its `scalar` must survive
}
