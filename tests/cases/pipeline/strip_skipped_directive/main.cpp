// SPDX-License-Identifier: GPL-3.0-or-later
#if 0
#define X /*conditional comment
kept to preserve skipped line counts*/ 1
#elif __LINE__ == 5
int main(){return 0;}
#else
int main(){return 1;}
#endif
