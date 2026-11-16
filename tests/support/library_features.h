// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/path.h"
#include "core/process/process.h"
#include <fstream>

namespace distill::testing {
// Probe the library API, independently of the fixture under test. A missing
// feature skips only its real-library build, never its extraction assertions.
inline Result<bool> libraryFeature(const std::string& name, const std::filesystem::path& gcc,
                                   const std::filesystem::path& work) {
  std::string source;
  if (name == "formatter") source = "#include <format>\nint main(){return std::format(\"{}\",7).empty();}\n";
  else if (name == "chrono_parse") source =
      "#include <chrono>\n#include <sstream>\nnamespace app {struct Date {};"
      "std::istream& from_stream(std::istream& in,const char*,Date&){return in;}}\n"
      "int main(){std::istringstream in; app::Date d; in>>std::chrono::parse(\"%Y\",d);}\n";
  else return Error{"unknown library feature: " + name};
  const auto input = work / ("probe-" + name + ".cpp");
  { std::ofstream f(input); f << source; if (!f) return Error{"cannot write library probe"}; }
  ProcessSpec spec; spec.cwd = work; spec.timeout = std::chrono::seconds(20);
  spec.argv = {pathToUtf8(gcc),"-std=gnu++23","-fsyntax-only",pathToUtf8(input)};
  auto result = runProcess(spec); if (!result.ok()) return result.error();
  const auto& r = result.value();
  if (r.timedOut || r.signal || r.stdoutTruncated || r.stderrTruncated) return Error{"library probe limit: " + r.stderrText};
  return r.exitCode == 0;
}
}
