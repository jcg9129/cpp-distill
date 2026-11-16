namespace lib {
    template<typename T>
    struct base_a {                      // grandparent
        void helper() const {}           // only named by the using-declaration below
        T twice(T x) const { return x + x; }
    };

    template<typename T>
    struct base_b : base_a<T> {};        // middle: a dependent base of calc

    template<typename T>
    class calc : base_b<T> {
        using base = base_b<T>;
        using base::helper;              // helper lives in the GRANDPARENT base_a
        using base::twice;

    public:
        T run(T x) const { return twice(x); }
    };
}

int main() { return lib::calc<int>{}.run(21); }   // expect 42
