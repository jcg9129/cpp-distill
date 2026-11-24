#define __need_size_t
#pragma push_macro("__need_size_t")
#include <stddef.h>
#pragma pop_macro("__need_size_t")
#include <stddef.h>
#ifdef __need_size_t
int main() { return 1; }
#else
int main() { return 0; }
#endif
