struct Point {
    int x;
    int y;
};

int unused() { return 7; }

int main() {
    Point p{20, 22};
    return p.x + p.y == 42 ? 0 : 1;
}
