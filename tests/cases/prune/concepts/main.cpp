// SPDX-License-Identifier: GPL-3.0-or-later
template<class T> concept Small = sizeof(T) <= 8;
template<class T> concept Unused = sizeof(T) == 1;
template<Small T> int run(T) { return 0; }
int main() { return run(1); }
