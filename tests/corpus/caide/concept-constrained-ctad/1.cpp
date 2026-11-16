#include <concepts>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace lib {
    // Used ONLY as a type-constraint on a class template parameter (never in a
    // function body or a requires-clause). It survives only if the dependency
    // edge template -> parameter -> concept exists.
    template <typename Op, typename T>
    concept combiner = std::regular_invocable<Op const &, T const &, T const &>;

    // Unused: must be removed.
    template <typename Op, typename T>
    concept splitter = std::regular_invocable<Op const &, T const &>;

    template <typename T, combiner<T> Op>
    class fold {
        Op op_;
        T acc_;

    public:
        // T is in a non-deduced context (type_identity_t), so class template
        // argument deduction for `fold` cannot use the implicit guide from this
        // constructor -- it must go through the user-defined guide below. If that
        // guide is removed as "unused", the CTAD in main() stops compiling.
        template <std::ranges::sized_range R>
            requires std::convertible_to<std::ranges::range_value_t<R>, T>
        fold(R &&values, std::type_identity_t<T> init, Op op)
            : op_(std::move(op)), acc_(std::move(init)) {
            for (auto &&v : values) acc_ = op_(acc_, static_cast<T>(v));
        }

        T value() const { return acc_; }
    };

    template <std::ranges::sized_range R, typename T, combiner<T> Op>
    fold(R &&, T, Op) -> fold<T, Op>;
}

int main() {
    std::vector<int> a{1, 2, 3, 4};
    lib::fold f(a, 0, std::plus<>{});
    return f.value();
}
