// SPDX-License-Identifier: GPL-3.0-or-later
struct R {};
template<class T> R helper(T) { return {}; }
template<> R helper<int>(int) { return {}; }
int main() { R r; (void)r; }
