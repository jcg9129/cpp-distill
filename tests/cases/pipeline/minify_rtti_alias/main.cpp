// SPDX-License-Identifier: GPL-3.0-or-later
#include <typeindex>
#include <cstdio>
using HiddenInfo = std::type_info;
using HiddenIndex = std::type_index;
HiddenInfo const* information = nullptr;
HiddenIndex* indexPointer = nullptr;
int main() { std::printf("%d\n", information == nullptr && indexPointer == nullptr); }
