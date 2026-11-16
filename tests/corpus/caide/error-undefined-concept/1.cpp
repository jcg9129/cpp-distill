#include <cstdint>
#include <type_traits>

namespace atl {
    using int128 = __int128_t;
    using uint128 = __uint128_t;
    namespace traits {
        template<typename Tp, typename... Types>
        struct is_one_of : public std::disjunction<std::is_same<Tp, Types>...> {};
        template<typename T>
        struct is_integral : public is_one_of<std::remove_cv_t<T>,
            signed int, unsigned int, signed long long, unsigned long long, int128, uint128> {};
        template<typename T> constexpr bool is_integral_v = is_integral<T>::value;
        template<typename T>
        struct is_unsigned_integral : public is_one_of<std::remove_cv_t<T>,
            unsigned int, unsigned long long, uint128> {};
        template<typename T> constexpr bool is_unsigned_integral_v = is_unsigned_integral<T>::value;
        template<typename T> struct make_signed : std::make_signed<T> {};
        template<typename T> requires std::is_same_v<std::remove_cv_t<T>, uint128>
        struct make_signed<T> { using type = int128; };
        template<typename T> using make_signed_t = typename make_signed<T>::type;
    }
    template<class T> concept integral = traits::is_integral_v<T>;
    template<class T> concept unsigned_integral = traits::is_unsigned_integral_v<T>;
}
using namespace atl;

// BUG under test: capitalized concept names are undefined (only lowercase exist).
template<Integral T>
struct GcdResult { T gcd; traits::make_signed_t<T> x, y; };

template<UnsignedIntegral U>
constexpr GcdResult<U> extended_gcd(const U &a, const U &b) {
    using S = traits::make_signed_t<U>;
    S x1 = 1; U a1 = a; (void)b; return {a1, x1, x1};
}

int main() { auto r = extended_gcd<unsigned long long>(12, 18); return (int)r.gcd; }
