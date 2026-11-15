// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <span>
namespace distill {
struct Resource {
  const char *path;
  const unsigned char *data;
  std::size_t size;
};
std::span<const Resource> resources();
}
