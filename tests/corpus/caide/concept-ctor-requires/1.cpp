#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>
#include <type_traits>
#include <vector>

namespace lib {
    // Reached ONLY through the constructor template's requires-clause below.
    // The base visitor does not traverse a constructor template's requires-clause,
    // so without the explicit walk this concept is pruned while the clause that
    // names it survives -> the output fails to compile.
    template <typename R>
    concept index_range = std::ranges::input_range<R>
                       && std::integral<std::remove_cvref_t<std::ranges::range_reference_t<R>>>;

    // Unused: must be removed.
    template <typename R>
    concept value_range = std::ranges::input_range<R>
                       && std::floating_point<std::remove_cvref_t<std::ranges::range_reference_t<R>>>;

    template <std::size_t N>
    class collector {
        std::vector<unsigned> data_;

    public:
        collector() = default;

        template <typename R>
            requires index_range<R>
        explicit collector(R &&items) {
            for (auto &&e : items) data_.push_back(unsigned(e) % N);
        }

        std::size_t size() const { return data_.size(); }
    };
}

int main() {
    std::vector<int> items{1, 2, 3, 27, 4};
    lib::collector<26> c(items);
    return int(c.size());
}
