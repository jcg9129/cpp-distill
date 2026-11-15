// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/profile/profile.h"
#include "core/profile/toml.h"
#include "core/path.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

namespace distill {
namespace {
Result<std::string> readFile(const std::filesystem::path& file) {
  std::ifstream input(file, std::ios::binary);
  if (!input) return Error{pathToUtf8(file) + ":1: cannot read file"};
  std::string text{std::istreambuf_iterator<char>(input), {}};
  if (input.bad()) return Error{pathToUtf8(file) + ":1: read failed"};
  return text;
}
bool matches(const std::string& text, const char* pattern) { return std::regex_match(text, std::regex(pattern)); }
bool clean(std::string_view text) {
  return std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
struct Schema {
  const TomlDocument& doc;
  std::string file;
  [[noreturn]] void fail(std::string_view key, const std::string& message) const {
    const auto it = doc.find(key);
    throw Error{file + ":" + std::to_string(it == doc.end() ? 1 : it->second.line) + ": " + std::string(key) + ": " + message};
  }
  template <class T> const T& get(std::string_view key) const {
    const auto it = doc.find(key);
    if (it == doc.end()) fail(key, "required key missing");
    const auto* value = std::get_if<T>(&it->second.value);
    if (!value) fail(key, "wrong type");
    return *value;
  }
  std::string string(std::string_view key) const {
    auto s = get<std::string>(key);
    if (s.empty() || !clean(s)) fail(key, "expected a non-empty string without control characters");
    return s;
  }
  void consistency(const Profile& p) const {
    const auto key = "predefined";
    auto expected = [&](std::string_view name, long value, bool required) {
      for (const auto& op : p.macroOperations) if (op.value.substr(0, op.value.find_first_of("(=")) == name) return;
      const auto found = p.predefined.find(name);
      if (found == p.predefined.end()) {
        if (required && p.snapshot && !p.snapshotFacts.contains(name)) fail(key, "snapshot missing required macro " + std::string(name));
        return;
      }
      auto replacement = std::string_view(found->second.replacement);
      const auto end = replacement.find_last_not_of(" \t");
      replacement = replacement.substr(0, end == replacement.npos ? 0 : end + 1);
      while (replacement.starts_with(' ') || replacement.starts_with('\t')) replacement.remove_prefix(1);
      while (!replacement.empty() && std::string_view("uUlL").find(replacement.back()) != std::string_view::npos) replacement.remove_suffix(1);
      long actual = 0;
      const auto parsed = std::from_chars(replacement.data(), replacement.data() + replacement.size(), actual);
      if (found->second.functionLike || parsed.ec != std::errc{} || parsed.ptr != replacement.data() + replacement.size() || actual != value)
        fail(key, "macro " + std::string(name) + " disagrees with triple/compiler/std");
    };
    expected("__GNUC__", p.compiler.major, true);
    expected("__GNUC_MINOR__", p.compiler.minor, true);
    if (p.patchSpecified) expected("__GNUC_PATCHLEVEL__", p.compiler.patch, true);
    expected("__cplusplus", p.dialect.cplusplus, true);
    if (p.predefined.contains("__clang__")) fail(key, "Clang snapshot supplied for GCC");
    if (p.dialect.gnu) {
      if (p.predefined.contains("__STRICT_ANSI__")) fail(key, "__STRICT_ANSI__ disagrees with GNU dialect");
    } else expected("__STRICT_ANSI__", 1, true);
    const bool x64 = p.triple.starts_with("x86_64-");
    const bool x86 = matches(p.triple, "i[3-6]86-.*");
    const bool arm64 = p.triple.starts_with("aarch64-") || p.triple.starts_with("arm64-");
    const bool win = p.triple.find("mingw") != p.triple.npos || p.triple.find("windows") != p.triple.npos;
    const bool linux = p.triple.find("linux") != p.triple.npos;
    const bool mac = p.triple.find("darwin") != p.triple.npos;
    for (auto [name, present] : {std::pair{"__x86_64__", x64}, {"__i386__", x86}, {"__aarch64__", arm64},
                              {"_WIN32", win}, {"_WIN64", win && (x64 || arm64)},
                              {"__linux__", linux}, {"__APPLE__", mac}}) {
      if (present) expected(name, 1, true);
      else if (p.predefined.contains(name)) fail(key, std::string(name) + " disagrees with triple");
    }
  }
  Profile parse(const std::filesystem::path& path, const SnapshotReader& reader) const {
    for (const auto& [key, entry] : doc) {
      (void)entry;
      if (key != "name" && key != "description" && key != "triple" && key != "compiler" && key != "std" && key != "flags" && key != "link" && key != "inert" && key != "build" && key != "snapshot_compiler" && key != "libraries" && key != "predefined" && key != "check" && key != "updated" && key != "sources") fail(key, "unknown key");
    }
    Profile p;
    p.name = string("name");
    if (!matches(p.name, "[a-z0-9-]+") || pathToUtf8(path.filename()) != p.name + ".toml") fail("name", "must be [a-z0-9-]+ and match the .toml file name");
    p.description = get<std::string>("description");
    if (p.description.empty()) fail("description", "must not be empty");
    p.triple = string("triple");
    if (!matches(p.triple, "[A-Za-z0-9_]+(-[A-Za-z0-9_.]+){2,}")) fail("triple", "invalid target triple");
    const auto& compiler = get<TomlTable>("compiler");
    if (compiler.size() != 2 || !compiler.contains("family") || !compiler.contains("version")) fail("compiler", "requires only family and version");
    if (compiler.at("family") != "gcc") fail("compiler", "only gcc is supported");
    p.compilerVersion = compiler.at("version");
    if (!matches(p.compilerVersion, "[0-9]+\\.[0-9]+(\\.[0-9]+)?")) fail("compiler", "expected major.minor[.patch]");
    p.compiler.family = CompilerFamily::gcc;
    std::istringstream version(p.compilerVersion);
    char dot;
    if (!(version >> p.compiler.major >> dot >> p.compiler.minor) || p.compiler.major < 1) fail("compiler", "invalid version components");
    p.patchSpecified = std::count(p.compilerVersion.begin(), p.compilerVersion.end(), '.') == 2;
    if (p.patchSpecified && !(version >> dot >> p.compiler.patch)) fail("compiler", "invalid patch version");
    const auto std = string("std");
    if (!matches(std, "(c|gnu)\\+\\+[0-9]{2}")) fail("std", "expected c++NN or gnu++NN");
    const auto code = std.substr(std.size() - 2);
    const std::map<std::string, long> dialects{{"98", 199711}, {"03", 199711}, {"11", 201103}, {"14", 201402}, {"17", 201703}, {"20", 202002}, {"23", 202302}, {"26", 202400}};
    if (!dialects.contains(code)) fail("std", "unsupported C++ dialect");
    p.dialect = {std, std.starts_with("gnu"), dialects.at(code)};
    p.flags = get<TomlStrings>("flags");
    if (doc.contains("link")) p.link = get<TomlStrings>("link");
    if (doc.contains("inert")) p.inert = get<TomlStrings>("inert");
    for (const auto* key : {"build", "snapshot_compiler"}) if (doc.contains(key)) {
      const auto& provenance = get<TomlTable>(key);
      if (provenance.size() != 1 || (!provenance.contains("package") && !provenance.contains("recipe"))) fail(key, "requires exactly package or recipe");
      const auto& [kind, value] = *provenance.begin();
      if (value.empty() || !clean(value) || (kind == "recipe" && !matches(value, "https?://[^ \\t]+")) ||
          (kind == "package" && !matches(value, "[A-Za-z0-9_.:+-]+=[A-Za-z0-9_.:+~-]+"))) fail(key, "invalid compiler provenance (package requires an exact version)");
      (std::string_view(key) == "build" ? p.build : p.snapshotCompiler) = {kind, value};
    }
    // Profile options are an argv, not response-file references. A synthetic
    // source lets us share the exact command classifier without filesystem reads.
    for (const auto& flag : p.flags) if (flag.empty() || !clean(flag) || flag.starts_with('@')) fail("flags", "invalid option/response file");
    std::vector<std::string> argv{"g++"};
    argv.insert(argv.end(), p.flags.begin(), p.flags.end()); argv.push_back("profile.cpp");
    auto command = parseCompileCommand(argv, path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path());
    if (!command.ok()) fail("flags", command.error().message);
    for (const auto& arg : command.value().args) {
      if (arg.option == "-std=") fail("flags", "-std= belongs in std");
      if (arg.classification == Class::input) continue;
      if (arg.classification == Class::controlled || arg.classification == Class::libraries || arg.option == "-I" || arg.option == "-iquote" || arg.option == "-isystem") fail("flags", "option belongs outside profile compile flags: " + arg.option);
      if (arg.classification == Class::unmodelled && std::find(p.inert.begin(), p.inert.end(), arg.spelling.front()) == p.inert.end()) fail("flags", "unmodelled option: " + arg.option);
    }
    std::set<std::string> distinctInert;
    for (const auto& option : p.inert) {
      if (!distinctInert.insert(option).second) fail("inert", "duplicate option: " + option);
      const auto found = std::find_if(command.value().args.begin(), command.value().args.end(), [&](const auto& arg) { return arg.spelling.front() == option && arg.classification == Class::unmodelled; });
      if (found == command.value().args.end()) fail("inert", "must name an unmodelled option present in flags: " + option);
    }
    p.macroOperations = command.value().localMacros();
    for (const auto& op : p.macroOperations) {
      if (op.option == "-U") {
        if (!matches(op.value, "[A-Za-z_][A-Za-z_0-9]*")) fail("flags", "invalid -U name");
        p.judgeMacros.erase(op.value);
      } else {
        const auto eq = op.value.find('=');
        const auto spelling = op.value.substr(0, eq);
        auto macro = parseMacros("#define " + spelling + " " + (eq == op.value.npos ? "1" : op.value.substr(eq + 1)) + "\n");
        if (!macro.ok() || macro.value().size() != 1) fail("flags", "invalid -D definition");
        p.judgeMacros.insert_or_assign(macro.value().begin()->first, macro.value().begin()->second);
      }
    }
    for (const auto& option : p.link) if (option.empty() || !clean(option) || !(option == "-static" || option == "-static-libstdc++" || option == "-static-libgcc" || option == "-s" || option.starts_with("-Wl,") || option.starts_with("-l") || option.starts_with("-L"))) fail("link", "expected linker-only option");
    p.libraries = get<TomlStrings>("libraries");
    if (p.libraries.empty()) fail("libraries", "select at least one extension list");
    std::set<std::string> selected;
    for (const auto& library : p.libraries) if ((library != "glibc" && library != "libstdc++" && library != "mingw-w64" && library != "macos-libc") || !selected.insert(library).second) fail("libraries", "unknown or duplicate extension-list library");
    p.check = get<TomlStrings>("check");
    for (const auto& compilerName : p.check) if (compilerName.empty() || !clean(compilerName)) fail("check", "empty or invalid compiler name");
    p.provenance = {path, string("updated"), get<TomlStrings>("sources")};
    if (!matches(p.provenance.updated, "[0-9]{4}-[0-9]{2}-[0-9]{2}")) fail("updated", "expected YYYY-MM-DD");
    const auto& date = p.provenance.updated;
    const std::chrono::year_month_day day{std::chrono::year{std::stoi(date.substr(0, 4))}, std::chrono::month{static_cast<unsigned>(std::stoi(date.substr(5, 2)))}, std::chrono::day{static_cast<unsigned>(std::stoi(date.substr(8, 2)))}};
    if (!day.ok() || int(day.year()) < 1) fail("updated", "invalid calendar date");
    if (p.provenance.sources.empty()) fail("sources", "must not be empty");
    for (const auto& url : p.provenance.sources)
      if (!matches(url, "https?://([A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?|\\[[0-9A-Fa-f:.]+\\])(:[0-9]{1,5})?([/?#][^ \\t\\r\\n]*)?") || !clean(url)) fail("sources", "expected an http(s) URL");
    if (doc.contains("predefined")) {
      p.snapshot = string("predefined");
      if (*p.snapshot == "." || *p.snapshot == ".." || p.snapshot->find_first_of("/\\:") != p.snapshot->npos) fail("predefined", "must be a file name relative to the profile");
      const auto snapshotPath = path.parent_path() / pathFromUtf8(*p.snapshot);
      if (!reader) fail("predefined", "snapshot missing: " + pathToUtf8(snapshotPath));
      auto contents = reader(snapshotPath);
      if (!contents.ok()) fail("predefined", "snapshot missing or unreadable: " + pathToUtf8(snapshotPath) + " (" + contents.error().message + ")");
      auto encoding = validateProfileUtf8(contents.value(), pathToUtf8(snapshotPath));
      if (!encoding.ok()) fail("predefined", encoding.error().message);
      auto parsed = parseProfileSnapshot(p, contents.value());
      if (!parsed.ok()) fail("predefined", pathToUtf8(snapshotPath) + ": " + parsed.error().message);
    }
    consistency(p);
    return p;
  }
};
} // namespace
Result<Profile> parseProfile(std::string_view text, const std::filesystem::path& file, SnapshotReader reader) {
  try {
    auto doc = parseProfileToml(text, pathToUtf8(file));
    if (!doc.ok()) return doc.error();
    return Schema{doc.value(), pathToUtf8(file)}.parse(file, reader);
  }
  catch (const Error& error) { return error; }
  catch (const std::exception& error) { return Error{pathToUtf8(file) + ":1: " + error.what()}; }
}
Result<Profile> loadProfile(const std::filesystem::path& file) {
  try {
    auto text = readFile(file);
    if (!text.ok()) return text.error();
    return parseProfile(text.value(), file, readFile);
  } catch (const std::exception& error) { return Error{pathToUtf8(file) + ":1: " + error.what()}; }
}
Result<bool> Profile::checkA3(const std::set<std::string, std::less<>>& userDefines) const {
  std::string collisions;
  for (const auto& name : userDefines) if (const auto fact = initialState(name); fact.state == MacroState::known || fact.state == MacroState::definedUnknown) {
    if (!collisions.empty()) collisions += ", ";
    collisions += name;
  }
  if (!collisions.empty()) return Error{pathToUtf8(provenance.file) + ":1: A3 conflict: user code defines judge macro(s): " + collisions};
  return true;
}
std::optional<std::filesystem::path> profileUserDirectory(const ProfileLookup& lookup) {
  if (lookup.userDirectory) return lookup.userDirectory;
  auto env = [&](const char* key) -> std::optional<std::filesystem::path> {
    auto it = lookup.environment.find(key);
    if (it == lookup.environment.end() || it->second.empty()) return {};
    return pathFromUtf8(it->second);
  };
  if (lookup.windows) {
    if (auto base = env("APPDATA")) return *base / "distill" / "judges";
  } else {
    if (auto base = env("XDG_CONFIG_HOME"); base && base->is_absolute()) return *base / "distill" / "judges";
    if (auto base = env("HOME")) return *base / ".config" / "distill" / "judges";
  }
  return {};
}
Result<Profile> lookupProfile(std::string_view nameOrFile, const ProfileLookup& lookup, const ProfileFiles& embedded) {
  try {
    if (nameOrFile.find_first_of("/\\") != nameOrFile.npos || nameOrFile.ends_with(".toml")) return loadProfile(pathFromUtf8(nameOrFile));
    const std::string name(nameOrFile);
    if (!matches(name, "[a-z0-9-]+")) return Error{name + ":1: invalid profile name"};
    const auto filename = name + ".toml";
    if (auto directory = profileUserDirectory(lookup)) {
      auto file = *directory / pathFromUtf8(filename);
      std::error_code ec;
      const auto status = std::filesystem::symlink_status(file, ec);
      if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) return Error{pathToUtf8(file) + ":1: " + ec.message()};
      if (std::filesystem::exists(status)) return loadProfile(file); // Invalid user profiles never fall through.
    }
    const auto found = embedded.find(filename);
    if (found == embedded.end()) return Error{name + ":1: judge profile not found"};
    const auto file = pathFromUtf8("embedded") / filename;
    return parseProfile(found->second, file, [&](const std::filesystem::path& snapshot) -> Result<std::string> {
      const auto it = embedded.find(pathToUtf8(snapshot.filename()));
      if (it == embedded.end()) return Error{"embedded snapshot missing"};
      return it->second;
    });
  } catch (const std::exception& error) { return Error{std::string(nameOrFile) + ":1: " + error.what()}; }
}
} // namespace distill
