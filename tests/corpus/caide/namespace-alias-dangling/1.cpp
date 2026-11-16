#include <cstdio>

namespace lib {
    namespace geometry {
        // Never referenced by main -> the whole geometry namespace is pruned.
        template <typename T>
        constexpr T twice(T v) {
            return v * 2;
        }
    }

    namespace io {
        inline void put(int x) {
            std::printf("%d\n", x);
        }
    }
}

using namespace lib;
// Alias to a namespace that survives (io::put is used): must be kept.
namespace io = lib::io;
// Alias to a namespace that is entirely pruned: must be removed too, otherwise
// it dangles ("'geometry' is not a namespace-name").
namespace geo = lib::geometry;

int main() {
    io::put(21);
    return 0;
}
