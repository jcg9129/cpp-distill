// A static_assert that is the sole substatement of a braceless `else` must not be
// removed outright, or the branch would be left empty (a syntax error). Its
// semicolon is kept as a null statement.
template<class T>
T pick() {
    if constexpr (sizeof(T) == 4)
        return T{};
    else
        static_assert(sizeof(T) == 4, "unsupported type");
}

int main() { return pick<int>(); }
