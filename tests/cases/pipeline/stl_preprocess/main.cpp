// SPDX-License-Identifier: GPL-3.0-or-later
#include <bits/stdc++.h>
int main(){
  std::vector<int> values{3,1,2};
  std::sort(values.begin(), values.end());
  std::optional<int> sum = std::accumulate(values.begin(), values.end(), 0);
  auto [first, last] = std::pair<int,int>{values.front(), values.back()};
  return *sum != 6 || first != 1 || last != 3;
}
