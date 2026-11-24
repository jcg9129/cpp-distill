#line 2 "main.cpp"
#define DISTILL_ONCE_0123456789abcdef
#line 4 "main.cpp"
#define PRESENT(a, b) defined(a ## b)
#if PRESENT(DISTILL_ONCE_, 0123456789abcdef)
int main() { return 0; }
#else
int main() { return 1; }
#endif
