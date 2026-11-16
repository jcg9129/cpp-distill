// SPDX-License-Identifier: GPL-3.0-or-later
#if __UNKNOWN
#if 0
int gone;
#endif
int a=__LINE__;
#else
#if 1
int b=__builtin_LINE();
#endif
#endif
int main(){return __LINE__!=13;}
