// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <utility>
#include <variant>

namespace distill {
struct Error { std::string message; };

// Like expected: accessing the wrong alternative is a caller error.
template <class T> class Result {
  std::variant<T, Error> storage_;
public:
  Result(T value) : storage_(std::move(value)) {}
  Result(Error error) : storage_(std::move(error)) {}
  bool ok() const noexcept { return std::holds_alternative<T>(storage_); }
  T& value() { return std::get<T>(storage_); }
  const T& value() const { return std::get<T>(storage_); }
  Error& error() { return std::get<Error>(storage_); }
  const Error& error() const { return std::get<Error>(storage_); }
};
} // namespace distill
