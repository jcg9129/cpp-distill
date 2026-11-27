// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/path.h"
#include "core/process/process.h"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {
int run(const std::vector<std::string>& argv) {
  if (argv.size() < 2) return 2;
  const auto& command = argv[1];
  if (command == "echo-args") {
    for (std::size_t i = 2; i < argv.size(); ++i) std::cout << argv[i].size() << ':' << argv[i] << '\n';
  } else if (command == "self-path") {
    auto self = distill::runningExecutable();
    if (!self.ok()) return 3;
    std::cout << distill::pathToUtf8(self.value()) << '\n';
  } else if (command == "exit" && argv.size() == 3) return std::stoi(argv[2]);
  else if (command == "signal") std::raise(SIGTERM);
  else if (command == "sleep" && argv.size() >= 3) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::stoll(argv[2])));
    if (argv.size() == 4) std::ofstream(distill::pathFromUtf8(argv[3])) << "survived";
  } else if (command == "spawn-sleeper" && argv.size() >= 3) {
#ifdef _WIN32
    // Use the executable's native path so tests also cover Unicode/spaced paths.
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length == executable.size()) return 3;
    executable.resize(length);
    auto line = L"\"" + executable + L"\" sleep " + std::to_wstring(std::stoll(argv[2]));
    if (argv.size() == 4) line += L" \"" + distill::pathFromUtf8(argv[3]).native() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) return 3;
    CloseHandle(process.hProcess); CloseHandle(process.hThread);
#else
    std::vector<std::string> childArgs{argv[0], "sleep", argv[2]};
    if (argv.size() == 4) childArgs.push_back(argv[3]);
    std::vector<char*> pointers;
    for (auto& arg : childArgs) pointers.push_back(arg.data());
    pointers.push_back(nullptr);
    pid_t child;
    if (posix_spawn(&child, childArgs[0].c_str(), nullptr, nullptr, pointers.data(), environ)) return 3;
#endif
    std::cout << "started\n" << std::flush;
  } else if (command == "flood" && argv.size() == 3) {
    auto left = std::stoull(argv[2]);
    const std::string chunk(8192, 'x');
    while (left) {
      const auto size = static_cast<std::streamsize>(std::min<unsigned long long>(left, chunk.size()));
      std::cout.write(chunk.data(), size);
      std::cerr.write(chunk.data(), size);
      left -= static_cast<unsigned long long>(size);
    }
  } else if (command == "cat-stdin") std::cout << std::cin.rdbuf();
#ifndef _WIN32
  else if (command == "fd-open" && argv.size() == 3)
    return fcntl(std::stoi(argv[2]), F_GETFD) >= 0 ? 1 : 0;
#endif
#ifdef _WIN32
  else if (command == "export-pipe-handles" && argv.size() == 3) {
    // Keep writers alive outside the runner's job, in the unit-test process.
    const auto parent = OpenProcess(PROCESS_DUP_HANDLE, FALSE, static_cast<DWORD>(std::stoul(argv[2])));
    if (!parent) return 3;
    HANDLE out = nullptr, err = nullptr;
    const bool copied = DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE),
        parent, &out, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
        DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE),
        parent, &err, 0, FALSE, DUPLICATE_SAME_ACCESS);
    CloseHandle(parent);
    if (!copied) return 3;
    std::cout << reinterpret_cast<std::uintptr_t>(out) << ' '
              << reinterpret_cast<std::uintptr_t>(err) << '\n' << std::flush;
  }
#endif
  else if (command == "-xc++" && argv.size() >= 5) {
    // A failing compiler stub exposes the probe source path to the test.
    std::cerr << argv[4];
    return 1;
  }
  else if (command == "pwd") std::cout << distill::pathToUtf8(std::filesystem::current_path()) << '\n';
  else return 2;
  return 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  _setmode(_fileno(stdin), _O_BINARY);
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) args.push_back(distill::pathToUtf8(std::filesystem::path(argv[i])));
  return run(args);
}
#else
int main(int argc, char** argv) { return run({argv, argv + argc}); }
#endif
