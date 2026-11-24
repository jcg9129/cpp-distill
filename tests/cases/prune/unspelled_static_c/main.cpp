// SPDX-License-Identifier: GPL-3.0-or-later
struct R {};
extern "C" {
static R helper() { return {}; }
extern "C++" { static R cpp_helper() { return {}; } }
}
int main() { R r; (void)r; }
