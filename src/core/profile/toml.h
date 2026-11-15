// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <cstdint>
#include <map>
#include <string_view>
#include <variant>
#include <vector>

namespace distill {
using TomlStrings = std::vector<std::string>;
using TomlTable = std::map<std::string, std::string, std::less<>>;
using TomlValue = std::variant<std::string, std::int64_t, bool, TomlStrings, TomlTable>;
struct TomlEntry { TomlValue value; std::size_t line; };
using TomlDocument = std::map<std::string, TomlEntry, std::less<>>;
// Deliberately a subset: flat assignments, single-line strings, TOML integers,
// booleans, string arrays and single-line inline tables with string values.
Result<bool> validateProfileUtf8(std::string_view text, std::string_view file);
Result<TomlDocument> parseProfileToml(std::string_view text, std::string_view file);
} // namespace distill
