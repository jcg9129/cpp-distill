// C++26 pack indexing: args...[I]
template<int I, typename... Ts>
auto pick(Ts... args) {
    return args...[I];
}

int unusedHelper(int x) {
    return x * 2;
}

int main() {
    return pick<1>(10, 20, 30);
}
