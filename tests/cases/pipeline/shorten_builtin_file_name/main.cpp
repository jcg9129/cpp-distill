// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" int puts(const char*);
int main() { const char* file_name = __builtin_FILE_NAME(); puts(file_name[0] ? "file" : "empty"); }
