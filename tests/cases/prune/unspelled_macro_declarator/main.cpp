// SPDX-License-Identifier: GPL-3.0-or-later
#define PTR *
struct R {};
// Interior expansions do not trigger the old boundary pin. D3f must still veto.
R helper(R PTR) { return {}; }
#define VALUE 1
// Member extraction may carve this method out of the helper's byte ownership.
R body_helper() {
  struct Local { int value() { return VALUE; } };
  return {};
}
int main() { R r; (void)r; }
