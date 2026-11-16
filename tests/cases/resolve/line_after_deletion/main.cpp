// SPDX-License-Identifier: GPL-3.0-or-later
#if 0
int dead;
#endif
static_assert(__LINE__==5);
int main(){return __builtin_LINE()!=6;}
