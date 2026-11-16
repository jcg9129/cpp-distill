// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
__attribute__((constructor)) void startup(){std::puts("startup");}
__attribute__((destructor)) void shutdown(){std::puts("shutdown");}
__attribute__((used)) int retained=42;
__attribute__((used)) void retainedFunction(){}
void unused(){}
int main(){return 0;}
