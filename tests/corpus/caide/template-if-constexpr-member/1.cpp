// The member template `read_float` is named only inside an `if constexpr` branch
// of `read_one` that is never instantiated (read_one is only used for int). It
// must still be kept, because `read_one`'s template body refers to it by name.
struct reader {
    template<class T>
    T read_float() { return T{}; }

    template<class T>
    T read_one() {
        if constexpr (sizeof(T) > 4)
            return read_float<T>();
        else
            return T{};
    }
};

int main() {
    reader r;
    return r.read_one<int>();
}
