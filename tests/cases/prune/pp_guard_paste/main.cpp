#line 2 "main.cpp"
#define DISTILL_ONCE_0123456789abcdef
#line 4 "main.cpp"
#define CAT_I(a,b) a##b
#define CAT(a,b) CAT_I(a,b)
#define STR_I(x) #x
#define STR(x) STR_I(x)
#include <cstdio>
int main(){std::printf("%s\n",STR(CAT(DISTILL_,ONCE_0123456789abcdef)));}
