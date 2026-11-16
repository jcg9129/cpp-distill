// SPDX-License-Identifier: GPL-3.0-or-later
#if 1
int line=__builtin_LINE();
#else
int line=__builtin_LINE();
#endif
int main(){return line!=3;}
