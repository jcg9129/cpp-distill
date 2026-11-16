int f(long) { return 20; }
int f(double) { return 22; }
int f(int) { return f(0L) + f(0.0); }

int unused() { return 7; }

int main() {
    return f(0) == 42 ? 0 : 1;
}
