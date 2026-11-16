// C++23 multidimensional subscript operator
struct Matrix {
    int data[4] = {1, 2, 3, 4};
    int operator[](int i, int j) {
        return data[i * 2 + j];
    }
};

int deadFn() {
    return 0;
}

int main() {
    Matrix m;
    return m[1, 1];
}
