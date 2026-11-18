// SPDX-License-Identifier: GPL-3.0-or-later
#include <source_location>
int main(){return std::source_location::current().line() != 3;}
