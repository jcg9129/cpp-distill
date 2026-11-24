// SPDX-License-Identifier: GPL-3.0-or-later
#include <coroutine>
#include <cstdio>
int observed = 0;
struct Task {
 struct promise_type {
  Task get_return_object() { return {}; }
  std::suspend_never initial_suspend() noexcept { return {}; }
  std::suspend_never final_suspend() noexcept { return {}; }
  void return_void() noexcept {}
  void unhandled_exception() noexcept {}
  std::suspend_never await_transform(std::suspend_never x) { ++observed; return x; }
 };
};
Task perform() { co_await std::suspend_never{}; co_return; }
int main() {
 (void)"promise_type get_return_object initial_suspend final_suspend return_void unhandled_exception await_ready await_suspend await_resume";
 perform(); std::printf("%d\n",observed); return observed != 1;
}
