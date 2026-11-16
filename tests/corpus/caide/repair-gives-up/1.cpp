int twice(int value) { return 2 * value; }

int unused() { return 7; }

int main() {
    return twice(21) == 42 ? 0 : 1;
}
