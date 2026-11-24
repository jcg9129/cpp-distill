// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>
extern "C" int puts(const char*);
int main() {
  const auto* location_info = OBSERVE_LOCATION();
  puts(location_info->_M_function_name);
}
