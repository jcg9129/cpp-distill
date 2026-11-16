// C++23 deducing this: explicit object parameter
struct Counter {
    int value = 0;
    auto& incr(this Counter& self) {
        ++self.value;
        return self;
    }
};

struct Unused {
    void nothing() {}
};

int main() {
    Counter c;
    c.incr().incr();
    return c.value;
}
