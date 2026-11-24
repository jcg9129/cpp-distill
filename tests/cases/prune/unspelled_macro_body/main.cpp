// SPDX-License-Identifier: GPL-3.0-or-later
struct R {};
#define ASSERT_LIKE(condition) ((void)(condition))
#define DEFAULT_VALUE(value) (value)
R body_macro_helper(R r) { ASSERT_LIKE(true); return r; }
R try_macro_helper(R r) try { ASSERT_LIKE(true); return r; } catch (...) { return {}; }
R default_macro_helper(R r, int = DEFAULT_VALUE(1)) { return r; }
// Braces in a default argument must not be mistaken for the function body.
R lambda_default_helper(R r, int = [] { return DEFAULT_VALUE(1); }()) { return r; }
R declared_macro_helper(R, int = DEFAULT_VALUE(1));
R deleted_helper(R) = delete;
R deleted_macro_helper(R, int = DEFAULT_VALUE(1)) = delete;
// Identifiers in macro bodies and discarded arguments still count as names.
R named_in_macro_body(R r) { return r; }
#define BODY_NAME named_in_macro_body
R named_in_macro_argument(R r) { return r; }
#define DISCARD(value) 0
int main() { R r; (void)r; (void)DISCARD(named_in_macro_argument); }
