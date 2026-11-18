// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/profile/profile.h"
#include "core/names/names.h"
#include <algorithm>
#include <charconv>
#include <regex>
#include <sstream>

namespace distill {
namespace {
std::string key(const CompilerIdentity& c) { return "gcc-" + std::to_string(c.major) + "." + std::to_string(c.minor); }
std::vector<std::string> dataLines(const std::string& file) {
  const auto& files = embeddedProfileFiles();
  const auto it = files.find(file);
  std::vector<std::string> result;
  if (it == files.end()) return result;
  std::istringstream input(it->second); std::string line;
  while (std::getline(input, line)) if (!line.empty() && !line.starts_with('#')) result.push_back(line);
  return result;
}
FactOrigin origin(const std::string& text) {
  if (text == "compiler-builtin") return FactOrigin::compilerBuiltin;
  if (text == "startup-header") return FactOrigin::startupHeader;
  if (text == "command-operation") return FactOrigin::commandOperation;
  if (text == "special-builtin") return FactOrigin::specialBuiltin;
  if (text == "native-model") return FactOrigin::nativeModel;
  throw Error{"invalid fact provenance: " + text};
}
bool auditedGcc(const Profile& p) {
  return p.compiler.family == CompilerFamily::gcc && p.compiler.major >= 13 && p.compiler.major <= 16;
}
bool nonGccIdentity(std::string_view name) {
  static const std::set<std::string, std::less<>> names{
    "_MSC_VER", "_MSC_FULL_VER", "_MSC_BUILD", "_M_X64", "_M_AMD64", "_M_IX86",
    "_M_ARM", "_M_ARM64", "_M_ARM64EC", "__clang__", "__clang_major__",
    "__clang_minor__", "__clang_patchlevel__", "__INTEL_COMPILER",
    "__INTEL_LLVM_COMPILER", "__NVCC__", "__CUDACC__", "__EMSCRIPTEN__"};
  return names.contains(name);
}
std::string constantType(std::string_view name) {
  for (const auto* base : {"INT8", "INT16", "INT32", "INT64", "UINT8", "UINT16", "UINT32", "UINT64", "INTMAX", "UINTMAX"})
    if (name == "__" + std::string(base) + "_C") return "__" + std::string(base) + "_TYPE__";
  return {};
}
std::optional<std::string> builtinObject(const Profile& p, const std::string& name) {
  const auto it = p.snapshotFacts.find(name);
  if (!approximateAllowed(name) || it == p.snapshotFacts.end()) return {};
  const auto& f = it->second;
  // -D of a type/width macro changes its spelling, not GCC's internal node.
  if (f.state != MacroState::known || !f.definition || f.provenance != std::vector{FactOrigin::compilerBuiltin}) return {};
  const auto& m = *f.definition;
  if (m.functionLike || m.variadic || !m.parameters.empty()) return {};
  return m.replacement;
}
std::optional<unsigned> builtinWidth(const Profile& p, const std::string& name) {
  const auto text = builtinObject(p, name);
  if (!text) return {};
  unsigned value = 0;
  const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
  if (error != std::errc{} || end != text->data() + text->size() || !value) return {};
  return value;
}
bool consistentConstant(const Profile& p, const std::string& typeName, const MacroFact& fact) {
  // GCC 15.2.0, 5115c7e447fc07457443df874bf57840e8316d5f:
  // gcc/c-family/c-cppbuiltin.cc, builtin_define_stdint_macros,
  // type_suffix and builtin_define_constants. See the 2026-11-18 audit.
  if (!auditedGcc(p) || fact.state != MacroState::known || !fact.definition ||
      fact.provenance != std::vector{FactOrigin::compilerBuiltin}) return false;
  const auto type = builtinObject(p, typeName);
  if (!type) return false;
  if (typeName != "__INTMAX_TYPE__" && typeName != "__UINTMAX_TYPE__") {
    auto least = typeName;
    least.insert(least.starts_with("__UINT") ? 6 : 5, "_LEAST");
    if (builtinObject(p, least) != type) return false;
  }
  std::string suffix;
  if (*type == "signed char" || *type == "short int" || *type == "int") suffix = "";
  else if (*type == "unsigned char" || *type == "short unsigned int") {
    const auto width = builtinWidth(p, *type == "unsigned char" ? "__SCHAR_WIDTH__" : "__SHRT_WIDTH__");
    const auto intWidth = builtinWidth(p, "__INT_WIDTH__");
    if (!width || !intWidth || *width > *intWidth) return false;
    suffix = *width < *intWidth ? "" : "U";
  } else if (*type == "unsigned int") suffix = "U";
  else if (*type == "long int") suffix = "L";
  else if (*type == "long unsigned int") suffix = "UL";
  else if (*type == "long long int") suffix = "LL";
  else if (*type == "long long unsigned int") suffix = "ULL";
  else return false;
  const auto& macro = *fact.definition;
  return macro.functionLike && !macro.variadic && macro.parameters == std::vector<std::string>{"c"} &&
      macro.replacement == (suffix.empty() ? "c" : "c ## " + suffix);
}
}
bool MacroFact::activeSpecialBuiltin() const {
  return state == MacroState::definedUnknown && std::find(provenance.begin(), provenance.end(), FactOrigin::specialBuiltin) != provenance.end();
}
std::set<std::string, std::less<>> specialBuiltins(const CompilerIdentity& compiler) {
  if (compiler.family != CompilerFamily::gcc) return {};
  const auto lines = dataLines("builtins/" + key(compiler) + ".txt");
  return {lines.begin(), lines.end()};
}
std::map<std::string, std::string, std::less<>> nativeDependencies(const CompilerIdentity& compiler) {
  std::map<std::string, std::string, std::less<>> result;
  if (compiler.family != CompilerFamily::gcc) return result;
  for (const auto& line : dataLines("native/" + key(compiler) + "-x86.txt")) {
    const auto space = line.find(' '); result.emplace(line.substr(0, space), line.substr(space + 1));
  }
  return result;
}
bool approximateAllowed(std::string_view name) {
  static const auto patterns = [] {
    std::vector<std::regex> result;
    for (const auto& line : dataLines("approximate-allowlist.txt")) result.emplace_back(line);
    return result;
  }();
  return std::any_of(patterns.begin(), patterns.end(), [&](const auto& pattern) { return std::regex_match(name.begin(), name.end(), pattern); });
}
bool compatibleProfileTriple(std::string_view expected, std::string_view actual) {
  auto normalized = [](std::string_view triple) {
    std::vector<std::string> parts; std::istringstream input{std::string(triple)}; std::string part;
    while (std::getline(input, part, '-')) parts.push_back(part);
    // GNU abbreviated triples omit the vendor; MinGW triples have three fields.
    if (parts.size() >= 4 || (parts.size() == 3 && parts.back() == "mingw32")) parts.erase(parts.begin() + 1);
    return parts;
  };
  return normalized(expected) == normalized(actual);
}
MacroFact Profile::initialState(std::string_view name) const {
  const auto found = snapshotFacts.find(name);
  if (found != snapshotFacts.end()) {
    auto fact = found->second;
    const bool command = std::find(fact.provenance.begin(), fact.provenance.end(), FactOrigin::commandOperation) != fact.provenance.end();
    if (reproduced || command) return fact;
    const auto type = constantType(name);
    const bool permitted = !type.empty() ? consistentConstant(*this, type, fact) :
        (!nonGccIdentity(name) || (auditedGcc(*this) && fact.state == MacroState::undefined));
    if (approximateAllowed(name) && permitted) { fact.provenance.push_back(FactOrigin::allowList); return fact; }
    return {MacroState::unknown, {}, fact.provenance};
  }
  // Profiles without snapshots still establish explicit ordered command operations.
  for (auto it = macroOperations.rbegin(); it != macroOperations.rend(); ++it) {
    const auto end = it->value.find_first_of("(=");
    if (it->value.substr(0, end) == name) {
      if (it->option == "-U") return {MacroState::undefined, {}, {FactOrigin::commandOperation}};
      return {MacroState::known, judgeMacros.at(std::string(name)), {FactOrigin::commandOperation}};
    }
  }
  if (!isReserved(name)) return {MacroState::undefined, {}, {FactOrigin::a5}};
  if (reproduced) return {MacroState::undefined, {}, {FactOrigin::a4}};
  if (snapshot && approximateAllowed(name) && constantType(name).empty() &&
      (!nonGccIdentity(name) || auditedGcc(*this))) return {MacroState::undefined, {}, {FactOrigin::allowList}};
  return {MacroState::unknown, {}, {}};
}
Result<bool> parseProfileSnapshot(Profile& p, std::string_view text) {
  try {
    std::map<std::string, std::string> metadata;
    std::vector<std::string> flags;
    std::map<std::string, bool> probes;
    const auto builtins = specialBuiltins(p.compiler);
    const auto native = nativeDependencies(p.compiler);
    bool header = true;
    std::istringstream input{std::string(text)}; std::string line;
    std::size_t number = 0;
    auto fail = [&](const std::string& message) { throw Error{"snapshot line " + std::to_string(number) + ": " + message}; };
    auto insert = [&](const std::string& name, MacroFact fact) {
      if (!p.snapshotFacts.emplace(name, std::move(fact)).second) fail("duplicate state for " + name);
    };
    while (std::getline(input, line)) {
      ++number;
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line == "# SPDX-License-Identifier: GPL-3.0-or-later") continue;
      if (header) {
        if (line == "# end-header") { header = false; continue; }
        const auto colon = line.find(": ");
        if (!line.starts_with("# ") || colon == line.npos || colon + 2 == line.size()) fail("invalid metadata");
        const auto field = line.substr(2, colon - 2), value = line.substr(colon + 2);
        if (field == "flag") flags.push_back(value);
        else if (!metadata.emplace(field, value).second) fail("duplicate metadata " + field);
        continue;
      }
      std::smatch match;
      if (std::regex_match(line, match, std::regex("# probe ([A-Za-z_][A-Za-z_0-9]*) (defined|undefined)"))) {
        if (!builtins.contains(match[1].str()) || !probes.emplace(match[1], match[2] == "defined").second) fail("invalid or duplicate special-builtin probe");
        continue;
      }
      if (std::regex_match(line, match, std::regex("# state ([A-Za-z_][A-Za-z_0-9]*) (undefined|defined-unknown|unknown) ([a-z-]+)"))) {
        auto source = origin(match[3]);
        const auto name = match[1].str();
        if (source == FactOrigin::specialBuiltin && !builtins.contains(name)) fail("unregistered special builtin");
        if (source == FactOrigin::nativeModel && !native.contains(name)) fail("name outside native model");
        insert(name, {match[2] == "undefined" ? MacroState::undefined : match[2] == "defined-unknown" ? MacroState::definedUnknown : MacroState::unknown, {}, {source}});
        continue;
      }
      auto source = FactOrigin::compilerBuiltin;
      if (line.starts_with("# startup-header stdc-predef.h ")) { source = FactOrigin::startupHeader; line.erase(0, std::string_view("# startup-header stdc-predef.h ").size()); }
      else if (line.starts_with("# command-operation ")) { source = FactOrigin::commandOperation; line.erase(0, 20); }
      else if (line.starts_with("# native-model ")) { source = FactOrigin::nativeModel; line.erase(0, 15); }
      auto macros = parseMacros(line + "\n");
      if (!line.starts_with("#define ") || !macros.ok() || macros.value().size() != 1) fail("invalid definition or record");
      const auto& [name, macro] = *macros.value().begin();
      if (source == FactOrigin::nativeModel && !native.contains(name)) fail("name outside native model");
      insert(name, {MacroState::known, macro, {source}});
      p.predefined.emplace(name, macro);
    }
    std::set<std::string> fields{"distill-snapshot", "compiler-path", "compiler-version", "gcc-version", "dumpmachine", "profile-triple", "build", "reproduced", "std", "builtin-registry", "native-model"};
    if (p.snapshotCompiler) fields.insert("snapshot-compiler");
    if (header || metadata.size() != fields.size()) fail("missing metadata/end-header");
    for (const auto& field : fields) if (!metadata.contains(field)) fail("missing metadata " + field);
    if (metadata.at("distill-snapshot") != "1" || metadata.at("std") != p.dialect.name || flags != p.flags || metadata.at("profile-triple") != p.triple || !compatibleProfileTriple(p.triple, metadata.at("dumpmachine"))) fail("format/command/triple mismatch");
    if (metadata.at("reproduced") != "yes" && metadata.at("reproduced") != "no") fail("invalid reproduced value");
    if (p.snapshotCompiler && metadata.at("snapshot-compiler") != p.snapshotCompiler->first + "=" + p.snapshotCompiler->second) fail("snapshot compiler mismatch");
    if (metadata.at("reproduced") == "yes" && !p.build) fail("reproduced snapshot requires build provenance");
    if (metadata.at("reproduced") == "yes" && p.snapshotCompiler && p.snapshotCompiler != p.build) fail("snapshot compiler is not the judge build");
    const auto actualVersion = metadata.at("gcc-version");
    if (actualVersion != p.compilerVersion && !(actualVersion.starts_with(p.compilerVersion + ".") && !p.patchSpecified)) fail("GCC version mismatch");
    if (metadata.at("builtin-registry") != (builtins.empty() ? "none" : key(p.compiler))) fail("builtin registry mismatch");
    if (probes.size() != builtins.size()) fail("missing special-builtin probes");
    for (const auto& [name, defined] : probes) {
      const auto found = p.snapshotFacts.find(name);
      if (found == p.snapshotFacts.end() || (defined != (found->second.state == MacroState::known || found->second.state == MacroState::definedUnknown))) fail("special-builtin state disagrees with probe: " + name);
    }
    const bool host = std::any_of(p.flags.begin(), p.flags.end(), [](const auto& f) { return f == "-march=native" || f == "-mtune=native" || f == "-mcpu=native"; });
    if (metadata.at("native-model") != (host ? key(p.compiler) + "-x86" : "none")) fail("native model mismatch");
    if (host) {
      if (native.empty() || !p.triple.starts_with("x86_64-")) fail("unsupported native model/ABI");
      const bool tuneOnly = std::find(p.flags.begin(), p.flags.end(), "-march=native") == p.flags.end();
      for (const auto& [name, scope] : native) if (!tuneOnly || scope == "tune") {
        const auto found = p.snapshotFacts.find(name);
        if (found == p.snapshotFacts.end() || (found->second.provenance.front() != FactOrigin::nativeModel && found->second.provenance.front() != FactOrigin::commandOperation)) fail("missing native state: " + name);
      }
    }
    for (const auto& [name, fact] : p.snapshotFacts) {
      const auto source = fact.provenance.front();
      if (source == FactOrigin::nativeModel && !host) fail("native record without host-dependent option");
      if ((source == FactOrigin::compilerBuiltin || source == FactOrigin::startupHeader) && fact.state != MacroState::known) fail("ordinary/header states require a definition");
      if (source == FactOrigin::specialBuiltin && (fact.state == MacroState::known || fact.state == MacroState::unknown)) fail("special states require an availability result");
      if (source == FactOrigin::commandOperation && fact.state != MacroState::known && fact.state != MacroState::undefined) fail("invalid command operation state");
    }
    // The snapshot is the effective command, not a default dump to merge later.
    std::map<std::string, LocalMacro> finalOperations;
    for (const auto& op : p.macroOperations) finalOperations.insert_or_assign(op.value.substr(0, op.value.find_first_of("(=")), op);
    for (const auto& [name, op] : finalOperations) {
      const auto it = p.snapshotFacts.find(name);
      if (it == p.snapshotFacts.end()) fail("missing command operation state: " + name);
      const auto& fact = it->second;
      if (fact.provenance.front() == FactOrigin::startupHeader) continue;
      if (fact.provenance.front() != FactOrigin::commandOperation || fact.state != (op.option == "-U" ? MacroState::undefined : MacroState::known)) fail("command state mismatch: " + name);
      if (op.option == "-D") {
        const auto& expected = p.judgeMacros.at(name); const auto& actual = *fact.definition;
        if (actual.replacement != expected.replacement || actual.functionLike != expected.functionLike || actual.parameters != expected.parameters || actual.variadic != expected.variadic) fail("command definition mismatch: " + name);
      }
    }
    for (const auto& [name, fact] : p.snapshotFacts) if (fact.provenance.front() == FactOrigin::commandOperation && !finalOperations.contains(name)) fail("command provenance without operation: " + name);
    p.snapshotMetadata = {metadata.at("compiler-path"), metadata.at("compiler-version"), actualVersion, metadata.at("dumpmachine"), metadata.at("build"), p.snapshotCompiler ? metadata.at("snapshot-compiler") : "", metadata.at("reproduced") == "yes"};
    p.reproduced = p.build && p.snapshotMetadata.reproduced && metadata.at("build") == p.build->first + "=" + p.build->second && !builtins.empty();
    return true;
  } catch (const Error& e) { return e; }
}
} // namespace distill
