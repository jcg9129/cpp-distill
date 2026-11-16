// C++23 static operator()
struct Adder {
    static int operator()(int a, int b) {
        return a + b;
    }
};

struct Dead {
    int x;
};

int main() {
    Adder add;
    return add(2, 3);
}
