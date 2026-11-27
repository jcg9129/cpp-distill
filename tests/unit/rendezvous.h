// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace test {
// Try to exercise overlap without requiring the scheduler to run all workers.
// A single deadline opens the gate permanently, including for late arrivals.
// In particular, cleanup must never need a worker that has not started yet.
class Rendezvous {
  std::mutex mutex_;
  std::condition_variable ready_;
  const unsigned participants_;
  const std::chrono::milliseconds timeout_;
  std::chrono::steady_clock::time_point deadline_{};
  unsigned arrived_ = 0;
  bool released_ = false, completed_ = false;
public:
  explicit Rendezvous(unsigned participants,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(250))
      : participants_(participants), timeout_(timeout) {}
  void arriveAndWait() {
    std::unique_lock lock(mutex_);
    if (released_) return;
    if (!arrived_++) deadline_ = std::chrono::steady_clock::now() + timeout_;
    if (arrived_ == participants_) completed_ = released_ = true;
    else if (!ready_.wait_until(lock, deadline_, [&] { return released_; })) released_ = true;
    ready_.notify_all();
  }
  bool completed() {
    std::lock_guard lock(mutex_);
    return completed_;
  }
};
} // namespace test
