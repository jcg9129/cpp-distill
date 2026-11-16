// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#ifdef FIXTURE
int main(){std::puts("fixture");return 0;}
#else
int main(){return 1;}
#endif
#ifdef LOCAL
#error local macros must be replaced
#endif
