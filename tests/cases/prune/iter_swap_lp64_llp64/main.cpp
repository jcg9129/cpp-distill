// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstddef>
#include <iterator>

namespace app {
int calls = 0;

struct It {
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;

    int* p;

    int& operator*() const { return *p; }

    It& operator++() { ++p; return *this; }
    It operator++(int) {
        It old = *this;
        ++*this;
        return old;
    }

    It& operator--() { --p; return *this; }
    It operator--(int) {
        It old = *this;
        --*this;
        return old;
    }

    friend bool operator==(It, It) = default;
};

template<class T>
    requires (sizeof(long) == 4 || sizeof(T) == 1)
void iter_swap(T a, T b) {
    int temporary = *a;
    *a = *b;
    *b = temporary;
    ++calls;
}
}

int main() {
    int a[] = {1, 2};
    std::ranges::reverse(app::It{a}, app::It{a + 2});
    return app::calls;
}
