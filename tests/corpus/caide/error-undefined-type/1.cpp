template<typename T>
struct Wrap {
    NoSuchType value;
};
int main() { Wrap<int> w; (void)w; return 0; }
