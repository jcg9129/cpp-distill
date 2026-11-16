#define SQ(x) ((x) * (x))

int unused() { return 7; }

int main() {
    return SQ(7) == 49 ? 0 : 1;
}
