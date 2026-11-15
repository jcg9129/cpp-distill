// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/inline/inline.h"
#include "core/lex/scan.h"
#include "core/path.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <set>

namespace distill {
namespace {
// Errors unwind only this private implementation; the public boundary catches all.
template<class T> T take(Result<T> result) {
  if (!result.ok()) throw result.error();
  return std::move(result.value());
}
struct Edge { HeaderLookup lookup; std::size_t node = 0; std::string name; bool angle = false; };
struct Node {
  UserFile info;
  std::string bytes;
  std::vector<std::size_t> newlines;
  ScanResult scan;
  std::map<std::size_t, Edge> includes;
  std::optional<std::size_t> once, guardDefine;
  std::string guard;
  std::vector<bool> resync;
};
std::string escaped(std::string_view text) {
  std::string result;
  for (char c : text) { if (c == '\\' || c == '"') result += '\\'; result += c; }
  return result;
}
bool beginsGroup(std::string_view name) { return name == "if" || name == "ifdef" || name == "ifndef"; }
struct Engine {
  const CompileCommand& cmd;
  const IncludeSearch& search;
  const InlineOptions& options;
  InlineResult result;
  std::vector<std::unique_ptr<Node>> nodes;
  std::map<std::string, std::size_t> loaded;
  std::vector<std::size_t> discovering;
  std::set<std::string, std::less<>> identifiers, undefined;
  std::map<FileIdentity, std::string> onceNames;
  struct Frame { std::size_t node, position; std::optional<std::size_t> guardEpoch; };
  std::vector<Frame> stack;
  std::size_t unsafeIncludes = 0;
  bool macroStackName = false;
  Engine(const CompileCommand& command, const IncludeSearch& includes, const InlineOptions& settings)
      : cmd(command), search(includes), options(settings) {}

  [[noreturn]] void fail(const Node& n, std::size_t line, std::string message) const {
    throw Error{n.info.fileName + ":" + std::to_string(line) + ": " + message};
  }
  void checkIdentifiers(Node& n) {
    for (const auto& token : n.scan.identifiers) {
      identifiers.insert(token.text);
      if (token.text == "__COUNTER__") result.flags.counter = true;
      if (token.text == "_Pragma") result.flags.pragmaOperator = true;
      if (token.text == "push_macro" || token.text == "pop_macro") macroStackName = true;
      for (std::string_view forbidden : {"__INCLUDE_LEVEL__", "__BASE_FILE__", "__builtin_COLUMN", "__builtin_source_location", "source_location"})
        if (token.text == forbidden) fail(n, token.line, "unsupported identifier: " + token.text);
    }
    for (const auto& line : n.scan.lines) {
      const auto& tokens = line.tokens;
      for (std::size_t i = 0; i + 2 < tokens.size(); ++i) {
        if ((tokens[i].text != "__has_include" && tokens[i].text != "__has_include_next") ||
            tokens[i + 1].text != "(" || tokens[i + 2].kind != TokenKind::header) continue;
        const auto& operand = tokens[i + 2].text;
        auto hit = take(lookupHeader(search, std::string_view(operand).substr(1, operand.size() - 2), operand.front() == '<', n.info.fileName));
        if (hit.found && !hit.system) fail(n, tokens[i].line, tokens[i].text + " names a user header: " + operand);
      }
    }
  }
  void analyze(Node& n) {
    checkIdentifiers(n);
    std::vector<std::size_t> groups;
    std::vector<bool> hadElse;
    std::vector<std::size_t> directives;
    std::optional<std::size_t> outerEnd;
    for (std::size_t i = 0; i < n.scan.lines.size(); ++i) {
      const auto& line = n.scan.lines[i];
      if (!line.directive) continue;
      const auto& d = *line.directive;
      directives.push_back(i);
      if (d.name == "include_next" || d.name == "import" || d.name == "line") fail(n, line.tokens.front().line, "unsupported #" + d.name);
      if (d.name == "undef" && !d.tokens.empty()) undefined.insert(d.tokens[0].text);
      if (d.name == "pragma") {
        if (d.tokens.size() >= 2 && d.tokens[0].text == "GCC" && d.tokens[1].text == "system_header")
          fail(n, line.tokens.front().line, "unsupported #pragma GCC system_header");
        if (!d.tokens.empty() && d.tokens[0].text == "once") {
          if (directives.size() != 1 || !groups.empty() || n.once || d.tokens.size() != 1)
            fail(n, line.tokens.front().line, "#pragma once must be the first directive and outside conditionals");
          n.once = i;
        }
        if (!d.tokens.empty() && d.tokens[0].text == "push_macro") result.flags.pushMacro = true;
        if (!d.tokens.empty() && d.tokens[0].text == "pop_macro") result.flags.popMacro = true;
      }
      if (beginsGroup(d.name)) { groups.push_back(i); hadElse.push_back(false); }
      else if (d.name == "elif" || d.name == "elifdef" || d.name == "elifndef" || d.name == "else") {
        if (groups.empty() || hadElse.back()) fail(n, line.tokens.front().line, "unbalanced conditional: #" + d.name);
        if (d.name == "else") hadElse.back() = true;
      } else if (d.name == "endif") {
        if (groups.empty()) fail(n, line.tokens.front().line, "unbalanced #endif");
        groups.pop_back(); hadElse.pop_back();
        if (groups.empty() && !outerEnd) outerEnd = i;
      }
    }
    if (!groups.empty()) fail(n, n.scan.lines[groups.back()].tokens.front().line, "unbalanced #if/#endif");
    if (n.once || directives.size() < 3) return;
    const auto first = directives.front(), last = directives.back();
    if (!outerEnd || *outerEnd != last || n.scan.lines[last].directive->name != "endif") return;
    // No tokens, including ordinary code, may occur outside the guard.
    for (std::size_t i = 0; i < first; ++i) if (!n.scan.lines[i].tokens.empty()) return;
    for (std::size_t i = last + 1; i < n.scan.lines.size(); ++i) if (!n.scan.lines[i].tokens.empty()) return;
    const auto& opening = *n.scan.lines[first].directive;
    const auto& t = opening.tokens;
    std::string guard;
    if (opening.name == "ifndef" && t.size() == 1 && t[0].kind == TokenKind::identifier) guard = t[0].text;
    if (opening.name == "if") {
      if (t.size() == 3 && t[0].text == "!" && t[1].text == "defined" && t[2].kind == TokenKind::identifier) guard = t[2].text;
      if (t.size() == 5 && t[0].text == "!" && t[1].text == "defined" && t[2].text == "(" && t[3].kind == TokenKind::identifier && t[4].text == ")") guard = t[3].text;
    }
    if (guard.empty()) return;
    const auto& define = *n.scan.lines[directives[1]].directive;
    if (define.name != "define" || define.tokens.empty() || define.tokens[0].text != guard) return;
    // An outer #else/#elif would make skipping the recursive include observable.
    unsigned depth = 0;
    for (auto i : directives) {
      const auto& name = n.scan.lines[i].directive->name;
      if (beginsGroup(name)) ++depth;
      else if (name == "endif") --depth;
      else if (depth == 1 && (name == "else" || name == "elif" || name == "elifdef" || name == "elifndef")) return;
    }
    n.guard = std::move(guard); n.guardDefine = directives[1];
  }
  std::size_t load(HeaderLookup hit, std::size_t depth) {
    if (depth > options.maxIncludeDepth) throw Error{hit.fileName + ":1: include depth limit exceeded"};
    const auto identity = take(fileIdentity(hit.path));
    for (auto id : discovering) if (nodes[id]->info.identity == identity) return id;
    const auto old = loaded.find(hit.fileName);
    if (old != loaded.end()) return old->second;
    const auto id = nodes.size();
    auto node = std::make_unique<Node>();
    node->info = UserFile{hit.path, hit.fileName, identity, 0, 0};
    std::ifstream input(hit.path, std::ios::binary);
    if (!input) throw Error{hit.fileName + ":1: cannot read user file"};
    node->bytes.assign(std::istreambuf_iterator<char>(input), {});
    if (input.bad()) throw Error{hit.fileName + ":1: cannot read user file"};
    node->info.size = node->bytes.size();
    for (std::size_t i = 0; i < node->bytes.size(); ++i) if (node->bytes[i] == '\n') node->newlines.push_back(i);
    auto scanned = scanDirectives(node->bytes);
    if (!scanned.ok()) throw Error{hit.fileName + ":" + scanned.error().message};
    node->scan = std::move(scanned.value());
    auto& n = *node;
    nodes.push_back(std::move(node)); loaded.emplace(hit.fileName, id); discovering.push_back(id);
    analyze(n);
    for (std::size_t i = 0; i < n.scan.lines.size(); ++i) {
      const auto& line = n.scan.lines[i];
      if (!line.directive || line.directive->name != "include") continue;
      const auto& t = line.directive->tokens;
      if (t.size() != 1 || t[0].kind != TokenKind::header || t[0].text.size() <= 2)
        fail(n, line.tokens.front().line, "computed or malformed #include operand");
      Edge edge;
      edge.name = t[0].text.substr(1, t[0].text.size() - 2); edge.angle = t[0].text.front() == '<';
      edge.lookup = take(lookupHeader(search, edge.name, edge.angle, n.info.fileName));
      if (!edge.lookup.found && !edge.angle) fail(n, line.tokens.front().line, "quoted include not found: " + edge.name);
      if (edge.lookup.found && !edge.lookup.system) edge.node = load(edge.lookup, depth + 1);
      n.includes.emplace(i, std::move(edge));
    }
    discovering.pop_back();
    return id;
  }
  void prepare() {
    for (auto& node : nodes) if (node->once) {
      auto& guard = onceNames[node->info.identity];
      if (guard.empty()) {
        std::uint64_t salt = 0;
        do { guard = onceGuardName(node->info.identity, salt++); }
        while (identifiers.contains(guard));
        identifiers.insert(guard); // Also avoid hash collisions between distinct files.
      }
      node->guard = guard;
    }
    // Mark every switch/close of every group containing any edit, including nested edits.
    for (auto& node : nodes) {
      auto& n = *node;
      n.resync.resize(n.scan.lines.size());
      struct Group { std::vector<std::size_t> switches; bool dirty = false; };
      std::vector<Group> groups;
      for (std::size_t i = 0; i < n.scan.lines.size(); ++i) {
        const auto& l = n.scan.lines[i];
        if (l.directive && beginsGroup(l.directive->name)) groups.push_back({{}, false});
        const auto include = n.includes.find(i);
        const bool edit = n.once == i || (include != n.includes.end() &&
            ((include->second.lookup.found && !include->second.lookup.system) || !include->second.angle));
        if (edit) for (auto& group : groups) group.dirty = true;
        if (!l.directive) continue;
        const auto& name = l.directive->name;
        if (name == "elif" || name == "elifdef" || name == "elifndef" || name == "else" || name == "endif") {
          groups.back().switches.push_back(i);
          if (name == "endif") {
            if (groups.back().dirty) for (auto pos : groups.back().switches) n.resync[pos] = true;
            groups.pop_back();
          }
        }
      }
    }
  }
  void append(std::string_view text, std::optional<SourceLocation> source = {}) {
    if (text.size() > options.maxOutputBytes - result.text.size()) throw Error{"output size limit exceeded"};
    for (char c : text) {
      if (result.text.empty() || result.text.back() == '\n') result.lineMap.push_back(source);
      result.text += c;
      if (c == '\n' && source) ++source->line;
    }
  }
  void newline() { if (!result.text.empty() && result.text.back() != '\n') append("\n"); }
  void marker(const Node& n, std::size_t line) {
    newline(); append("#line " + std::to_string(line) + " \"" + escaped(n.info.fileName) + "\"\n");
  }
  std::size_t physicalAt(const Node& n, std::size_t offset) const {
    return 1 + static_cast<std::size_t>(std::lower_bound(n.newlines.begin(), n.newlines.end(), offset) - n.newlines.begin());
  }
  void original(const Node& n, std::size_t a, std::size_t b) {
    // BOMs are meaningful only at the start of a translation unit; remove them
    // from copied input before generated markers (the scanner already ignores them).
    if (a == 0 && n.bytes.starts_with("\xef\xbb\xbf")) a = 3;
    if (a < b) append(std::string_view(n.bytes).substr(a, b - a), SourceLocation{n.info.fileName, physicalAt(n, a)});
  }
  std::string cycle(const Node& n) const {
    std::string text = "recursive inclusion cycle: ";
    for (const auto& frame : stack) text += nodes[frame.node]->info.fileName + " -> ";
    return text + n.info.fileName;
  }
  bool recursive(std::size_t id) {
    auto& n = *nodes[id];
    for (const auto& frame : stack) if (nodes[frame.node]->info.identity == n.info.identity) {
      const auto& target = *nodes[frame.node];
      std::optional<std::string_view> library;
      if (options.library) library = *options.library;
      if (target.guard.empty() || !frame.guardEpoch || *frame.guardEpoch != unsafeIncludes ||
          undefined.contains(target.guard) || macroStackName || result.flags.pragmaOperator ||
          !options.names.protectedUserMacro(target.guard, library)) {
        const auto& parent = *nodes[stack.back().node];
        fail(parent, parent.scan.lines[stack.back().position].tokens.front().line, cycle(n));
      }
      return true;
    }
    return false;
  }
  void render(std::size_t id) {
    auto& n = *nodes[id];
    if (recursive(id)) { append("\n"); return; }
    if (stack.size() >= options.maxIncludeDepth) throw Error{n.info.fileName + ":1: include depth limit exceeded"};
    ++n.info.times;
    stack.push_back({id, 0, {}});
    if (n.once) {
      newline(); append("#ifndef " + n.guard + "\n#define " + n.guard + "\n");
      stack.back().guardEpoch = unsafeIncludes;
    }
    marker(n, 1);
    for (std::size_t i = 0; i < n.scan.lines.size(); ++i) {
      const auto& l = n.scan.lines[i];
      stack.back().position = i;
      if (n.guardDefine == i) stack.back().guardEpoch = unsafeIncludes;
      auto include = n.includes.find(i);
      const bool user = include != n.includes.end() && include->second.lookup.found && !include->second.lookup.system;
      const bool rewrite = include != n.includes.end() && !user && !include->second.angle;
      if (include != n.includes.end() && !user) {
        const auto& e = include->second;
        const bool standard = isStandardHeader(e.name);
        result.systemIncludes.push_back({{n.info.fileName, l.tokens.front().line}, e.name, e.angle, rewrite, e.lookup.found, standard, e.lookup.systemDirectory});
        if (!standard) ++unsafeIncludes;
      }
      if (user || rewrite || n.once == i) {
        const auto& d = *l.directive;
        original(n, l.begin, d.begin);
        if (user) { newline(); render(include->second.node); }
        else if (rewrite) {
          // Keep prefix/suffix comments intact, including comments spanning lines.
          append("#include <" + include->second.name + ">", SourceLocation{n.info.fileName, physicalAt(n, d.begin)});
        }
        else append("\n", SourceLocation{n.info.fileName, physicalAt(n, d.begin)});
        marker(n, physicalAt(n, d.end));
        original(n, d.end, l.end);
      } else if (n.resync[i]) {
        const auto& d = *l.directive;
        original(n, l.begin, d.end);
        marker(n, physicalAt(n, d.end));
        original(n, d.end, l.end);
      } else original(n, l.begin, l.end);
    }
    if (n.once) { newline(); append("#endif\n"); }
    stack.pop_back();
  }
  InlineResult run() {
    HeaderLookup root{cmd.cwd / cmd.source, pathToUtf8(cmd.source), true, false, {}};
    load(std::move(root), 1);
    prepare(); render(0);
    for (const auto& node : nodes) if (node->info.times) result.userFiles.push_back(node->info);
    return std::move(result);
  }
};
} // namespace
Result<InlineResult> inlineSources(const CompileCommand& cmd, const IncludeSearch& search, const InlineOptions& options) {
  try { return Engine{cmd, search, options}.run(); }
  catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("inline: ") + e.what()}; }
  catch (...) { return Error{"inline: unknown failure"}; }
}
} // namespace distill
