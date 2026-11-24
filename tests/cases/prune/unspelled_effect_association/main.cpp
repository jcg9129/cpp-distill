// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct Reducer { static inline int reducer_ = std::puts("registered"); };
struct Carrier {};
template<class T> int associated_helper(Carrier) { return Reducer<T>::reducer_; }
// No spelled use of associated_helper: only the type-association edge can
// carry this dormant trigger into rule 6's reverse dependency closure.
void dormant() { Carrier carrier; (void)carrier; }
int main() { std::puts("ok"); }
