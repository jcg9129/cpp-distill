// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept {};
Kept swap(Kept) { return {}; }
Kept begin(Kept) { return {}; }
Kept get(Kept) { return {}; }
bool operator==(Kept, Kept) { return true; }
int plain(Kept) { return 1; }
int dependent(Kept) { return 1; }
// distill: keep
template<class T> int patterns(T t) { return plain(Kept{}) + dependent(t); }
int macro_only(Kept) { return 1; }
#define UNUSED_MACRO macro_only(Kept{})
int argument_only(Kept) { return 1; }
#define DISCARD(x) 0
static_assert(DISCARD(argument_only) == 0);
int pasted_call(Kept) { return 1; }
#define CAT(a,b) a##b
// distill: keep
template<class T> int pasted(T t) { return CAT(past,ed_call)(t); }
namespace ns { int using_target(Kept) { return 1; } }
using ns::using_target;
Kept friended(Kept) { return {}; }
struct Friend { friend Kept friended(Kept); };
int callback(Kept) { return 1; }
int default_arg(Kept) { return 1; }
int takes_default(int x = default_arg(Kept{})) { return x; }
int template_arg(Kept) { return 1; }
template<int (*F)(Kept) = template_arg> struct Defaults {};
Kept local_target(Kept) { return {}; }
void local_user() { extern Kept local_target(Kept); }
int main() {
  Kept k; Friend f; Defaults<> d; (void)f; (void)d;
  auto address = &callback;
  if (address(k) != 1 || takes_default() != 1) return 1;
  std::puts("ok");
}
