#include <cstddef>
#include <vector>

namespace lib {
    template<typename T>
    struct storage_base {
    protected:
        std::vector<T> data_;

        void fill(std::size_t n, T const &v) { data_.assign(n, v); }

        T const &at(std::size_t i) const { return data_[i]; }

        // Named by the derived class's `using base::reserve;` but called by no
        // reachable code. It must survive anyway: the using-declaration that names
        // it is kept, and would dangle if the member were removed. In a class
        // template the base is dependent, so that using-declaration is an
        // UnresolvedUsingValueDecl with no resolved target -- the dependency edge
        // has to be recovered by looking `reserve` up in the base's pattern.
        void reserve(std::size_t n) { data_.reserve(n); }
    };

    template<typename T>
    class bag : storage_base<T> {
        using base = storage_base<T>;
        using base::at;
        using base::data_;
        using base::fill;
        using base::reserve;

    public:
        bag(std::size_t n, T v) { fill(n, v); }

        T const &top() const { return at(data_.size() - 1); }
    };
}

int main() {
    lib::bag<int> b(3, 5);
    return b.top();
}
