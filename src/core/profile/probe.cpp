// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/profile/probe.h"
#include "core/profile/sha256.h"
#include "core/lex/scan.h"
#include "core/names/names.h"
#include "core/path.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace distill {
namespace {
constexpr std::size_t valueLimit = 256;
constexpr std::string_view nativeMarker = "native";
Result<std::string> unhex(std::string_view s) {
  if (s.size() % 2) return Error{"odd hex length"};
  std::string out;
  for (std::size_t i = 0; i < s.size(); i += 2) {
    auto digit = [](char c) { auto p = std::string_view("0123456789abcdef").find(c); return p == std::string_view::npos ? -1 : int(p); };
    int a = digit(s[i]), b = digit(s[i+1]);
    if (a < 0 || b < 0) return Error{"invalid hex"};
    out += char(a * 16 + b);
  }
  return out;
}
std::uint32_t checksum(std::string_view bytes) {
  std::uint32_t h = 2166136261u;
  for (unsigned char c : bytes) h = (h ^ c) * 16777619u;
  return h;
}
std::string quote(std::string_view s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '\\' || c == '"') { out += '\\'; out += char(c); }
    else if (c < 32 || c == 127) {
      out += '\\'; out += char('0' + (c >> 6)); out += char('0' + ((c >> 3) & 7)); out += char('0' + (c & 7));
    } else out += char(c);
  }
  return out + '"';
}
std::size_t number(std::string_view s) {
  std::size_t n = 0;
  auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  if (s.empty() || r.ec != std::errc{} || r.ptr != s.data() + s.size() || std::to_string(n) != s)
    throw Error{"format error: invalid decimal"};
  return n;
}
std::vector<std::string> fields(std::string_view s) {
  std::vector<std::string> out;
  for (;;) {
    auto pos = s.find('|'); out.emplace_back(s.substr(0, pos));
    if (pos == s.npos) return out;
    s.remove_prefix(pos + 1);
  }
}
const std::set<std::string, std::less<>> contextual{
  "__LINE__", "__COUNTER__", "__FILE__", "__BASE_FILE__", "__INCLUDE_LEVEL__",
  "__DATE__", "__TIME__", "__TIMESTAMP__"};
MacroTable macros(const Profile& p) {
  auto result = p.predefined;
  for (const auto& [n, f] : p.snapshotFacts) if (f.definition) result[n] = *f.definition;
  for (const auto& [n, m] : p.judgeMacros) if (!p.snapshotFacts.contains(n)) result[n] = m;
  return result;
}
struct Audit { std::string reason; bool native = false; };
Audit audit(const Profile& p, const MacroTable& table, const std::string& name, std::set<std::string>& visiting) {
  if (contextual.contains(name)) return {"contextual field", false};
  if (name == "_Pragma") return {"_Pragma", false};
  if (name.starts_with("__distill_probe_")) return {"generated helper dependency", false};
  if (!visiting.insert(name).second) return {"recursive expansion", false};
  struct Erase { std::set<std::string>& s; const std::string& n; ~Erase() { s.erase(n); } } erase{visiting, name};
  Audit out;
  if (const auto f = p.snapshotFacts.find(name); f != p.snapshotFacts.end()) {
    out.native = std::find(f->second.provenance.begin(), f->second.provenance.end(), FactOrigin::nativeModel) != f->second.provenance.end()
      && (f->second.state == MacroState::unknown || f->second.state == MacroState::definedUnknown);
  }
  auto m = table.find(name);
  if (m == table.end()) {
    if (specialBuiltins(p.compiler).contains(name)) out.reason = "active special builtin";
    return out;
  }
  auto scan = scanDirectives(m->second.replacement);
  if (!scan.ok()) return {"unscannable replacement", out.native};
  int depth = 0;
  for (const auto& line : scan.value().lines) for (const auto& t : line.tokens) {
    if (t.text == "(") ++depth;
    if (t.text == ")" && --depth < 0) return {"unbalanced parentheses", out.native};
    if (t.text == "#" || t.text == "##" || t.text == "%:%:") return {"token construction", out.native};
    if (t.kind == TokenKind::identifier && std::find(m->second.parameters.begin(), m->second.parameters.end(), t.text) == m->second.parameters.end()) {
      auto child = audit(p, table, t.text, visiting);
      out.native |= child.native;
      if (!child.reason.empty()) return {child.reason, out.native};
    }
  }
  if (depth) out.reason = "unbalanced parentheses";
  return out;
}
bool nativePresence(const Profile& p, const std::string& name, const std::string& v, bool& applies) {
  applies = false;
  auto it = p.snapshotFacts.find(name);
  if (it == p.snapshotFacts.end()) return false;
  const auto& f = it->second;
  if (std::find(f.provenance.begin(), f.provenance.end(), FactOrigin::nativeModel) == f.provenance.end()) return false;
  // Known and absent joined facts are model invariants: compare normally.
  if (f.state == MacroState::known || f.state == MacroState::undefined) return false;
  applies = true;
  if (f.state == MacroState::unknown) return v == "0" || v == "1";
  return v == (f.state == MacroState::undefined ? "0" : "1");
}
// Exactly the facts whose expected value the comparison does not consult.
// Presence-only/unsupported expansions still compare their diagnostic reason.
bool nativeExpected(const Profile& p, const MacroTable& table, const JudgeProbeFact& f) {
  if (!f.unsupported.empty()) return false;
  if (f.category == "presence") {
    bool applies;
    nativePresence(p, f.name, "0", applies);
    return applies;
  }
  if (f.category == "expansion") {
    std::set<std::string> visiting;
    return audit(p, table, f.name, visiting).native;
  }
  return false;
}
std::vector<std::pair<std::string, std::string>> versions(const Profile& p) {
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto& lib : p.libraries) {
    if (lib == "libstdc++") { result.emplace_back("bits/c++config.h", "__GLIBCXX__"); result.emplace_back("bits/c++config.h", "_GLIBCXX_RELEASE"); }
    if (lib == "glibc") { result.emplace_back("features.h", "__GLIBC__"); result.emplace_back("features.h", "__GLIBC_MINOR__"); }
    if (lib == "mingw-w64") { result.emplace_back("_mingw.h", "__MINGW64_VERSION_MAJOR"); result.emplace_back("_mingw.h", "__MINGW64_VERSION_MINOR"); }
    if (lib == "macos-libc") result.emplace_back("Availability.h", "__MAC_OS_X_VERSION_MAX_ALLOWED");
  }
  return result;
}
std::vector<JudgeProbeFact> facts(const Profile& p) {
  const auto table = macros(p);
  auto names = specialBuiltins(p.compiler);
  for (const auto& [n, f] : p.snapshotFacts) { (void)f; names.insert(n); }
  for (const auto& [n, m] : table) { (void)m; names.insert(n); }
  for (const char* n : {"ONLINE_JUDGE", "EVAL", "LOCAL", "_DEBUG", "DEBUG", "ATCODER", "CODEFORCES", "NDEBUG", "__clang__"}) names.insert(n);
  std::vector<JudgeProbeFact> out;
  for (const auto& n : names) out.push_back({"presence", n, {}});
  for (const auto& [n, m] : table) {
    std::set<std::string> visiting;
    const auto a = audit(p, table, n, visiting);
    out.push_back({"expansion", n, m.functionLike ? "function-like; presence only" : a.reason});
  }
  for (const auto& [n, f] : p.snapshotFacts) if (!table.contains(n) && !specialBuiltins(p.compiler).contains(n)) {
    (void)f;
    out.push_back({"expansion", n, "no fixed snapshot definition; presence only"});
  }
  // Special-builtin identity/activity is deliberately never inferred from presence.
  for (const auto& n : specialBuiltins(p.compiler)) out.push_back({"builtin", n, "activity and definition untested; presence only"});
  auto headers = standardHeaders();
  headers.push_back("ext/pb_ds/assoc_container.hpp");
  headers.push_back("ext/pb_ds/tree_policy.hpp");
  for (const auto& [header, n] : versions(p)) { (void)n; headers.push_back(header); }
  std::sort(headers.begin(), headers.end()); headers.erase(std::unique(headers.begin(), headers.end()), headers.end());
  for (const auto& h : headers) out.push_back({"header", h, {}});
  for (const auto& [h, n] : versions(p)) out.push_back({"version", h + ":" + n, {}});
  out.push_back({"assumption", "coverage", "selected names only; A1/A4/A5 unvalidated; custom test is not grading evidence"});
  return out;
}
std::string factKey(const JudgeProbeFact& f) { return f.category + "|" + f.name; }
std::string factHash(const std::vector<JudgeProbeFact>& fs) {
  std::string text;
  for (const auto& f : fs) text += factKey(f) + "|" + f.unsupported + "\n";
  return detail::sha256(text);
}
// The header names the probe format, not the distill version: expected output
// comes from a CI build and must stay valid for released binaries. Any change
// to the generated fact list or encoding must bump the format number.
std::string header(const Profile& p) {
  return "DPROBE|1|" + p.name + "|" + p.provenance.contentDigest;
}
JudgeProbe makeProbe(const Profile& p, JudgeProbePart part, const std::vector<JudgeProbeFact>& all) {
  JudgeProbe result;
  const auto begin = all.size() * (part.number - 1) / part.total, end = all.size() * part.number / part.total;
  result.facts.assign(all.begin() + begin, all.begin() + end);
  result.header = header(p);
  result.manifest = "PART|" + std::to_string(part.number) + "|" + std::to_string(part.total) + "|" + factHash(result.facts) + "|" + p.provenance.contentDigest + "|" + factHash(all);
  const auto table = macros(p);
  const bool hasNative = std::any_of(result.facts.begin(), result.facts.end(),
      [&](const auto& f) { return nativeExpected(p, table, f); });
  std::ostringstream out;
  out << "// distill diagnostic judge probe; output is user-reported evidence only\n";
  std::vector<std::string> helpers{"raw", "string", "presence", "values", "keys", "put", "line", "hash", "count", "emit", "c", "s", "key", "value", "n"};
  if (hasNative) for (const char* h : {"expected", "argc", "argv"}) helpers.emplace_back(h);
  std::size_t versionCount = 0;
  for (const auto& f : result.facts) if (f.category == "version") ++versionCount;
  for (std::size_t i = 0; i < versionCount; ++i) out << "#ifdef __distill_probe_version_" << i << "\n#error distill probe helper collision\n#endif\n";
  for (const auto& h : helpers) out << "#ifdef __distill_probe_" << h << "\n#error distill probe helper collision\n#endif\n";
  // No include or macro definition may precede completion of these captures.
  out << "static const char* const __distill_probe_presence[]={\n";
  for (const auto& f : result.facts) if (f.category == "presence")
    out << "#ifdef " << f.name << "\n\"1\",\n#else\n\"0\",\n#endif\n";
  out << "0};\n// presence complete\n#define __distill_probe_raw(...) #__VA_ARGS__\n#define __distill_probe_string(...) __distill_probe_raw(__VA_ARGS__)\n";
  out << "static const char* const __distill_probe_values[]={\n";
  for (const auto& f : result.facts) if (f.category == "expansion" || f.category == "builtin") {
    if (!f.unsupported.empty()) out << quote("unsupported:" + f.unsupported) << ",\n";
    else out << "#ifdef " << f.name << "\n__distill_probe_string(" << f.name << "),\n#else\n\"undefined\",\n#endif\n";
  }
  // Headers are tested without including them. Version captures follow explicit
  // library-specific includes and record their stage in the key.
  for (const auto& f : result.facts) if (f.category == "header")
    out << "#if defined(__has_include)\n#if __has_include(<" << f.name << ">)\n\"1\",\n#else\n\"0\",\n#endif\n#else\n\"unsupported:__has_include unavailable\",\n#endif\n";
  out << "0};\n";
  std::size_t vi = 0;
  for (const auto& f : result.facts) if (f.category == "version") {
    auto colon = f.name.find(':'); auto h = f.name.substr(0, colon), n = f.name.substr(colon+1);
    out << "#if defined(__has_include)\n#if __has_include(<" << h << ">)\n#include <" << h << ">\n#endif\n#endif\n"
        << "static const char* const __distill_probe_version_" << vi++ << "=\n#ifdef " << n << "\n__distill_probe_string(" << n << ")\n#else\n\"unsupported:version unavailable after " << h << "\"\n#endif\n;\n";
  }
  if (hasNative) out << "#include <cstring>\n";
  out << R"cpp(#include <cstdio>
static unsigned long __distill_probe_hash=2166136261UL, __distill_probe_count=0;
static void __distill_probe_put(unsigned char __distill_probe_c){std::putchar(__distill_probe_c);__distill_probe_hash=((__distill_probe_hash^__distill_probe_c)*16777619UL)&0xffffffffUL;}
static void __distill_probe_line(const char* __distill_probe_s){while(*__distill_probe_s)__distill_probe_put((unsigned char)*__distill_probe_s++);__distill_probe_put(10);++__distill_probe_count;}
static void __distill_probe_emit(const char* __distill_probe_key,const char* __distill_probe_value){
unsigned __distill_probe_n=0;while(__distill_probe_n<=256 && __distill_probe_value[__distill_probe_n])++__distill_probe_n;
if(__distill_probe_n>256)__distill_probe_value="!probe-failure:value exceeds 256 bytes";
while(*__distill_probe_key)__distill_probe_put((unsigned char)*__distill_probe_key++);__distill_probe_put('|');
while(*__distill_probe_value){unsigned char __distill_probe_c=(unsigned char)*__distill_probe_value++;__distill_probe_put("0123456789abcdef"[__distill_probe_c>>4]);__distill_probe_put("0123456789abcdef"[__distill_probe_c&15]);}
__distill_probe_put(10);++__distill_probe_count;}
)cpp";
  if (hasNative)
    out << "int main(int __distill_probe_argc,char** __distill_probe_argv){\nconst bool __distill_probe_expected=__distill_probe_argc==2 && std::strcmp(__distill_probe_argv[1],\"--distill-expected\")==0;\n";
  else out << "int main(){\n";
  out << "__distill_probe_line(" << quote(result.header) << ");\n__distill_probe_line(" << quote(result.manifest) << ");\n";
  std::size_t pi = 0, ei = 0; vi = 0;
  for (const auto& f : result.facts) {
    std::string value;
    if (f.category == "presence") value = "__distill_probe_presence[" + std::to_string(pi++) + "]";
    else if (f.category == "version") value = "__distill_probe_version_" + std::to_string(vi++);
    else if (f.category == "assumption") value = quote("unsupported:" + f.unsupported);
    else value = "__distill_probe_values[" + std::to_string(ei++) + "]";
    // CI runs the same binary twice: real output for validation, then expected
    // output with a raw non-hex marker. Ordinary judge runs keep real values.
    if (nativeExpected(p, table, f))
      out << "if(__distill_probe_expected)__distill_probe_line(" << quote(factKey(f) + '|' + std::string(nativeMarker)) << ");else\n";
    out << "__distill_probe_emit(" << quote(factKey(f)) << "," << value << ");\n";
  }
  out << "std::printf(\"END|%lu|%lu\\n\",__distill_probe_count,__distill_probe_hash);\nreturn std::ferror(stdout)?1:0;}\n";
  result.source = out.str();
  // Allow CRLF emitted by a Windows C runtime as well as LF.
  result.outputBound = result.header.size() + result.manifest.size() + 64 + result.facts.size() + 3;
  for (const auto& f : result.facts) {
    const std::size_t maxValue = (f.category == "presence" ? 1 : f.category == "header" ? 40 : valueLimit);
    // Cover both the real hex value and the expected-only raw marker.
    const auto encodedSize = std::max(2 * maxValue, nativeExpected(p, table, f) ? nativeMarker.size() : 0);
    result.outputBound += factKey(f).size() + 2 + encodedSize;
  }
  return result;
}
struct ParsedPart { JudgeProbePart part; std::map<std::string, std::string> values; };
std::vector<ParsedPart> parseOutput(const Profile& p, std::string_view text, bool expected) {
  if (text.empty()) throw Error{"probe failure: empty output (probe may not have compiled or run)"};
  std::vector<ParsedPart> out;
  while (!text.empty()) {
    const auto originalSize = text.size();
    std::string covered;
    auto line = [&]() {
      auto n = text.find('\n');
      if (n == text.npos) throw Error{"format error: truncated output"};
      std::string s(text.substr(0,n)); text.remove_prefix(n+1);
      if (!s.empty() && s.back() == '\r') s.pop_back();
      return s;
    };
    auto h = line();
    if (h != header(p)) throw Error{"format error: wrong probe header (format/version/profile/digest)"};
    covered += h + '\n';
    auto manifest = line(); covered += manifest + '\n';
    auto m = fields(manifest);
    if (m.size() != 6 || m[0] != "PART") throw Error{"format error: missing manifest"};
    ParsedPart parsed{{number(m[1]), number(m[2])}, {}};
    auto probe = generateJudgeProbe(p, parsed.part);
    if (!probe.ok() || probe.value().manifest != manifest) throw Error{"format error: mixed or invalid part manifest"};
    std::size_t count = 2;
    for (;;) {
      auto s = line(); auto f = fields(s);
      if (f[0] == "END") {
        if (f.size() != 3 || number(f[1]) != count || number(f[2]) != checksum(covered)) throw Error{"format error: trailer line count/checksum"};
        break;
      }
      if (f.size() != 3 || (f[0] != "presence" && f[0] != "expansion" && f[0] != "builtin" && f[0] != "header" && f[0] != "version" && f[0] != "assumption")) throw Error{"format error: malformed fact/category"};
      // Real values are always hex encoded (including the text "native").
      // Only expected files may contain the raw marker, at permitted keys.
      if (f[2] == nativeMarker) {
        const auto table = macros(p);
        const auto& fs = probe.value().facts;
        auto fact = std::find_if(fs.begin(), fs.end(), [&](const auto& fact) {
          return fact.category == f[0] && fact.name == f[1];
        });
        if (!expected || fact == fs.end() || !nativeExpected(p, table, *fact))
          throw Error{"format error: native marker only allowed for native-model expected facts"};
      }
      auto value = f[2] == nativeMarker ? Result<std::string>{std::string(nativeMarker)} : unhex(f[2]);
      if (!value.ok() || value.value().size() > valueLimit) throw Error{"format error: invalid fact encoding/length"};
      if (!parsed.values.emplace(f[0] + "|" + f[1], value.value()).second) throw Error{"format error: duplicate fact"};
      covered += s + '\n'; ++count;
    }
    if (originalSize - text.size() > p.probeOutputBytes) throw Error{"format error: output byte budget exceeded"};
    out.push_back(std::move(parsed));
  }
  return out;
}
std::map<std::string, std::string> collect(const Profile& p, const std::vector<std::string>& files, bool expected) {
  std::map<std::string, std::string> out;
  std::set<std::size_t> seen;
  std::size_t total = 0;
  for (const auto& file : files) for (auto& part : parseOutput(p, file, expected)) {
    if (!total) total = part.part.total;
    if (total != part.part.total || !seen.insert(part.part.number).second) throw Error{"format error: duplicate or mixed parts"};
    const auto probe = generateJudgeProbe(p, part.part).value();
    std::set<std::string> allowed;
    for (const auto& f : probe.facts) allowed.insert(factKey(f));
    for (auto& [k,v] : part.values) {
      if (!allowed.contains(k)) {
        if (expected) throw Error{"probe failure: unexpected fact in shipped expected output"};
        // An extra unknown key is reported per fact, but a key belonging to
        // another part is a mixed manifest, even when the union is complete.
        for (const auto& f : facts(p)) if (factKey(f) == k) throw Error{"format error: fact in wrong part"};
      }
      if (!out.emplace(k,v).second) throw Error{"format error: duplicate fact across parts"};
    }
    if (expected && part.values.size() != allowed.size()) throw Error{"probe failure: incomplete shipped expected output"};
  }
  if (!total || seen.size() != total) throw Error{"format error: missing parts"};
  return out;
}
} // namespace
Result<JudgeProbePart> parseJudgeProbePart(std::string_view text) {
  try {
    auto slash = text.find('/');
    if (slash == text.npos) return Error{"part must be N/M"};
    JudgeProbePart part{number(text.substr(0,slash)), number(text.substr(slash+1))};
    if (!part.number || part.number > part.total || part.total > 10000) return Error{"part must satisfy 1 <= N <= M <= 10000"};
    return part;
  } catch (const Error& e) { return e; }
}
Result<JudgeProbe> generateJudgeProbe(const Profile& p, JudgeProbePart part) {
  auto all = facts(p);
  if (!part.number || part.number > part.total || part.total > all.size()) return Error{"invalid probe part count"};
  auto result = makeProbe(p,part,all);
  if (result.source.size() > p.probeSourceBytes || result.outputBound > p.probeOutputBytes)
    return Error{"probe exceeds source/output byte budget; use --part N/M with a larger M (all M parts required)"};
  return result;
}
JudgeCheck checkJudgeProbe(const Profile& p, std::string_view expected, const std::vector<std::string>& outputs) {
  JudgeCheck result;
  auto fail = [&](const std::string& text) { result.passed = false; result.report += text + '\n'; };
  if (expected.empty()) {
    fail("no expected output shipped for " + p.name + "; run profiles.yml");
    result.report += "checked 0, skipped 0\n";
    return result;
  }
  try {
    auto e = collect(p,{std::string(expected)},true), j = collect(p,outputs,false);
    const auto table = macros(p);
    for (const auto& f : facts(p)) {
      const auto key = factKey(f);
      auto want = e.find(key), got = j.find(key);
      if (want == e.end()) { fail("probe failure: expected output missing " + key); continue; }
      if (got == j.end()) { fail("missing " + key + " [probe failure]"); continue; }
      const auto& v = got->second;
      if (v.starts_with("!probe-failure:")) { fail("unsupported " + key + " [probe failure]: " + v); ++result.skipped; continue; }
      if (f.category == "presence" && v != "0" && v != "1") { fail("format error: invalid presence " + key); continue; }
      if (!f.unsupported.empty() || want->second.starts_with("unsupported:")) {
        ++result.skipped;
        if (v != want->second) fail("unsupported " + key + (f.unsupported == "contextual field" ? " [contextual field]: expected " : " [probe failure]: expected ") + quote(want->second) + " judge " + quote(v));
        else result.report += "unsupported " + key + ": " + v + '\n';
        continue;
      }
      if (v.starts_with("unsupported:")) { ++result.skipped; fail("unsupported " + key + " [probe failure]: " + v); continue; }
      ++result.checked;
      bool applies = false, match = v == want->second;
      if (f.category == "presence") {
        bool permitted = nativePresence(p, f.name, v, applies);
        if (applies) match = permitted;
      }
      if (f.category == "expansion") {
        std::set<std::string> visiting;
        if (audit(p, table, f.name, visiting).native) { match = true; applies = true; }
      }
      bool contradiction = false;
      if (f.category == "presence" && v == "1") {
        auto saved = p.snapshotFacts.find(f.name);
        contradiction = (saved != p.snapshotFacts.end() && saved->second.state == MacroState::undefined) ||
            (saved == p.snapshotFacts.end() && p.initialState(f.name).state == MacroState::undefined);
        if (contradiction) match = false;
      }
      if (match) { result.report += "match " + key + (applies ? " (native model permitted state)\n" : "\n"); continue; }
      std::string reason = "fact mismatch";
      if (contradiction) reason = "contradicted assumption";
      if (f.name == "__GNUC__" || f.name == "__GNUC_MINOR__" || f.name == "__GNUC_PATCHLEVEL__" || f.name == "__cplusplus" || f.name == "__STRICT_ANSI__" || f.name == "__clang__") reason = "wrong compiler or option selected";
      fail("mismatch " + key + " [" + reason + "]: expected " + quote(contradiction ? "0 (profile treats name as undefined)" : want->second) + " judge " + quote(v));
    }
    for (const auto& [k,v] : j) if (!e.contains(k)) fail("extra " + k + " [fact mismatch]: judge " + quote(v));
  } catch (const Error& error) { fail(error.message); }
  result.report += "checked " + std::to_string(result.checked) + ", skipped " + std::to_string(result.skipped) + '\n';
  result.report += result.passed ? "user-reported match; scope: custom test of this option, observation date only\n" : "user-reported observation failed; investigate before release; no automatic downgrade\n";
  return result;
}
Result<std::string> judgeProbeExpected(const Profile& p) {
  if (p.probeExpected) {
    try { collect(p, {*p.probeExpected}, true); }
    catch (const Error& error) { return error; }
    return *p.probeExpected;
  }
  return Error{"no expected output shipped for " + p.name + "; run profiles.yml"};
}
Result<std::filesystem::path> recordJudgeObservation(const Profile& p, const JudgeCheck& check,
    const std::vector<std::string>& outputs, const std::filesystem::path& directory, std::string date) {
  try {
    if (date.empty()) {
      auto day = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
      std::ostringstream s; s << std::setfill('0') << std::setw(4) << int(day.year()) << '-' << std::setw(2) << unsigned(day.month()) << '-' << std::setw(2) << unsigned(day.day()); date = s.str();
    }
    if (date.size() != 10 || date[4] != '-' || date[7] != '-' || date.find_first_not_of("0123456789-") != date.npos)
      return Error{"observation date must be YYYY-MM-DD"};
    std::filesystem::create_directories(directory);
    for (std::size_t n = 1; n < 1000000; ++n) {
      auto file = directory / pathFromUtf8(p.name + '-' + date + '-' + std::to_string(n) + ".txt");
      std::ostringstream out;
      out << "distill-observation: 1\nprofile: " << p.name << "\ndigest: " << p.provenance.contentDigest
          << "\nselector: " << p.provenance.selector << "\norigin: " << p.provenance.origin << ' ' << pathToUtf8(p.provenance.file)
          << "\ndate: " << date << "\nscope: custom test of this option, that date\nstatus: " << (check.passed ? "user-reported match" : "user-reported failure")
          << "\n" << check.report << "A1/A4/A5 remain assumptions; pasted output is not authenticated.\n";
      for (const auto& output : outputs) out << "--- judge output ---\n" << output;
      const auto bytes = out.str();
#ifdef _WIN32
      HANDLE stream = CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
      if (stream == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) continue;
        return Error{"cannot create observation " + pathToUtf8(file)};
      }
      bool written = true;
      for (std::size_t offset = 0; offset < bytes.size();) {
        DWORD count = 0;
        const auto size = static_cast<DWORD>(std::min<std::size_t>(65536,bytes.size()-offset));
        if (!WriteFile(stream,bytes.data()+offset,size,&count,nullptr) || count != size) { written = false; break; }
        offset += count;
      }
      const bool closed = CloseHandle(stream) != 0;
#else
      auto* stream = std::fopen(file.c_str(),"wbx");
      if (!stream) {
        if (errno == EEXIST) continue;
        return Error{"cannot create observation " + pathToUtf8(file)};
      }
      const bool written = std::fwrite(bytes.data(),1,bytes.size(),stream) == bytes.size();
      const bool closed = std::fclose(stream) == 0;
#endif
      if (!written || !closed) return Error{"cannot write observation " + pathToUtf8(file)};
      return file;
    }
    return Error{"no free observation filename"};
  } catch (const std::exception& e) { return Error{e.what()}; }
}
std::string latestJudgeObservation(const Profile& p, const ProfileFiles& files) {
  std::pair<std::string,std::size_t> latest;
  std::string summary = "-";
  const auto prefix = "observations/" + p.name + "-";
  for (const auto& [name,text] : files) {
    if (!name.starts_with(prefix) || !name.ends_with(".txt")) continue;
    auto suffix = name.substr(prefix.size(),name.size()-prefix.size()-4);
    if (suffix.size() < 12 || suffix[10] != '-') continue;
    std::size_t sequence = 0;
    try { sequence = number(std::string_view(suffix).substr(11)); } catch (const Error&) { continue; }
    if (!sequence) continue;
    const auto date = suffix.substr(0,10);
    if (date[4] != '-' || date[7] != '-' || date.find_first_not_of("0123456789-") != date.npos) continue;
    auto identity = std::pair{date,sequence};
    if (!text.starts_with("distill-observation: 1\n") || text.find("\nprofile: " + p.name + "\n") == text.npos ||
        text.find("\ndigest: " + p.provenance.contentDigest + "\n") == text.npos) continue;
    auto pos = text.find("\nstatus: ");
    if (pos == text.npos || identity <= latest) continue;
    auto begin = pos + 9, end = text.find('\n',begin);
    const auto status = text.substr(begin,end-begin);
    if (status != "user-reported match" && status != "user-reported failure") continue;
    latest = identity; summary = name.substr(13) + ": " + status;
  }
  return summary;
}
} // namespace distill
