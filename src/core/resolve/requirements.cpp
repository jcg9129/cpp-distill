// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/resolve/resolve.h"
#include <charconv>
#include <algorithm>
#include <limits>
#include <sstream>

namespace distill {
std::string isolatedConditionSource(const ConditionInput& in) {
  std::set<std::string, std::less<>> names{
    "__STDC__", "__STDC_HOSTED__", "__cplusplus", "__STDC_UTF_16__", "__STDC_UTF_32__",
    "__LINE__", "__FILE__", "__DATE__", "__TIME__", "__TIMESTAMP__", "__COUNTER__",
    "__BASE_FILE__", "__INCLUDE_LEVEL__", "__FILE_NAME__", "__has_include", "__has_include_next",
    "__has_builtin", "__has_attribute", "__has_cpp_attribute", "__has_feature", "__has_extension",
    "__has_embed", "__is_identifier", "__building_module", "__has_warning"};
  for (const auto& [file, text] : embeddedProfileFiles()) if (file.starts_with("builtins/")) {
    std::istringstream lines(text); std::string line;
    while (std::getline(lines, line)) if (!line.empty() && !line.starts_with('#')) names.insert(line);
  }
  std::string source;
  for (const auto& name : names) source += "#undef " + name + '\n';
  return source + in.source;
}
EvaluationRequirements profileRequirements(const Profile& p) {
  EvaluationRequirements r; r.dialect = p.dialect.name;
  auto commandFact = [](const MacroFact& f) {
    return std::find(f.provenance.begin(), f.provenance.end(), FactOrigin::commandOperation) != f.provenance.end();
  };
  auto number = [&](const char* name) -> std::optional<unsigned> {
    const auto f = p.initialState(name);
    if (commandFact(f)) return {}; // -D/-U can lie about physical representation.
    if (f.state != MacroState::known || !f.definition) return {};
    const auto& s = f.definition->replacement;
    unsigned n = 0; auto parsed = std::from_chars(s.data(), s.data() + s.size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size()) return {};
    return n;
  };
  auto signedness = [&](const char* name) -> std::optional<bool> {
    const auto f = p.initialState(name);
    if (commandFact(f)) return {};
    if (f.state == MacroState::undefined) return true;
    if (f.state == MacroState::known || f.state == MacroState::definedUnknown) return false;
    return {};
  };
  auto cb = number("__CHAR_BIT__"), i = number("__SIZEOF_INT__"), l = number("__SIZEOF_LONG__"),
       ll = number("__SIZEOF_LONG_LONG__"), w = number("__SIZEOF_WCHAR_T__"), im = number("__INTMAX_WIDTH__");
  auto cs = signedness("__CHAR_UNSIGNED__"), ws = signedness("__WCHAR_UNSIGNED__");
  if (cb && *cb && i && l && ll && w && im && cs && ws &&
      std::max({*i, *l, *ll, *w}) <= std::numeric_limits<unsigned>::max() / *cb)
    r.integers = IntegerModel{*cb, *i * *cb, *l * *cb, *ll * *cb, *w * *cb, *im, *cs, *ws};
  return r;
}
Result<std::string> evaluationTriple(const IntegerModel& m) {
  // All entries have 8-bit char, 32-bit int, 64-bit long long and intmax_t.
  // -f[un]signed-char supplies char signedness separately.
  if (m.charBits != 8 || m.intBits != 32 || m.longLongBits != 64 || m.intmaxBits != 64)
    return Error{"unsupported preprocessor integer model"};
  if (m.longBits == 64 && m.wcharBits == 32 && m.wcharSigned) return std::string("x86_64-unknown-linux-gnu");
  if (m.longBits == 64 && m.wcharBits == 32 && !m.wcharSigned) return std::string("aarch64-unknown-linux-gnu");
  if (m.longBits == 32 && m.wcharBits == 32 && m.wcharSigned) return std::string("i386-unknown-linux-gnu");
  if (m.longBits == 32 && m.wcharBits == 16 && !m.wcharSigned) return std::string("x86_64-w64-windows-gnu");
  return Error{"unsupported preprocessor integer model"};
}
} // namespace distill
