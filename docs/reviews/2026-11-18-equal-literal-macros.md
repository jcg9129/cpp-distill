<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Equal-literal predefined macros (2026-11-18)

## Problem

Pruning compares every predefined macro expanded in user code (outside `#if`)
token by token between the Clang parse and the judge snapshot
(`equalMacro`, `src/core/prune/prune.cpp`). GCC and Clang spell the limit
macros differently, so any use of `CHAR_MAX`, `INT_MAX`, `SIZE_MAX`, ...
disables pruning: `predefined macro __SCHAR_MAX__ differs: judge 0x7f, parse 127`.
This blocks 5 of 54 examples of a real library.

Comparing `cses-gcc13-cpp23-predefined.txt` with Clang 22's x86_64 predefines,
36 object-like macros differ only in the spelling of one integer literal
(`__SCHAR_MAX__`, `__SHRT_MAX__`, `__INT_MAX__`, `__LONG_MAX__`, `__LONG_LONG_MAX__`,
`__SIZE_MAX__`, `__UINT32_MAX__` (`0xffffffffU`/`4294967295U`), `__INT64_MAX__`,
`__WCHAR_MAX__`, `__WINT_MAX__`, the `__INT*_MAX__`/`__UINT*_MAX__` families, ...).
Each has the same value; all have the same type except two that the rule below
correctly rejects: `__cpp_concepts` (`202002L` long vs `202002` int) and
`__STDCPP_DEFAULT_NEW_ALIGNMENT__` (`16` int vs `16UL`). The `__INT_FAST16/32`
families also differ in value and stay rejected. No differing macro needs a
parenthesised or negated literal (`__WCHAR_MIN__`-style expressions are
token-identical), so the relaxation is limited to a **single literal token**.

## Rule (policy D3d-4)

A predefined user expansion with non-identical parse and judge definitions is
accepted only when all of these hold:

1. Both definitions are known, object-like, and consist of exactly one token
   that is an integer literal: decimal, octal (`0...`) or hexadecimal (`0x...`),
   with an optional standard suffix (`u`, `l`, `ll` in either case and order;
   `lL`/`Ll` rejected). Binary literals, digit separators, `z`/`uz`, `wb` and
   user-defined suffixes are rejected (not needed, and keeping the classifier
   minimal keeps it auditable).
2. The values are equal and fit in 64 bits.
3. Both literals get the same type per [lex.icon] Table 8, computed with the
   integer widths of **both** the parse target (reported by the worker from
   Clang's `TargetInfo`) and the judge profile (`__CHAR_BIT__` x
   `__SIZEOF_INT__/__SIZEOF_LONG__/__SIZEOF_LONG_LONG__`, not from `-D`/`-U`, as in
   `profileRequirements`). All four (literal, model) classifications must agree.
   Decimal unsuffixed: int, long, long long; octal/hex unsuffixed: int, unsigned
   int, long, unsigned long, long long, unsigned long long; `u` restricts to the
   unsigned types, `l`/`ll` start at that rank. If the value fits none of the
   listed standard types (where [lex.icon]/3 permits an extended integer type,
   or where GCC/Clang fall back to `unsigned long long`/`__int128` with a
   diagnostic), the literal is unclassified and the macro is rejected. Unknown
   or > 64-bit widths are rejected.
4. The expansion is not exposed to spelling-sensitive preprocessing:
   - not a token-paste participant (`paste` false), and
   - `hashOperator` false: the worker sets it when any macro whose definition
     contains `#`, `#@` or `##` (of any provenance: source, system header,
     predefined) is expanded inside the same physical user invocation span. This
     is a conservative over-approximation of "the literal's spelling reaches a
     `#`/`##` operand": an argument is only stringized/pasted unexpanded, so a
     predefined literal can reach such an operand only through an enclosing
     function-like invocation (`XSTR(CHAR_MAX)` -> `STR(127)`), which always lies
     within one physical invocation span in user text, and the inner macro with
     the operator expands inside that span. Object-like macros with a `#`
     punctuator are counted too (harmless over-approximation).
   - not inside a user preprocessing directive (`sourceDirectives`): e.g.
     `#line CHAR_MAX` requires a decimal digit-sequence, and a computed
     `#include` spells a header name from token spellings. `#if`/`#elif`
     conditions are a separate path (`condition`) and unchanged.

Everything else is unchanged: function-like predefined macros, string,
floating and character literals, and `__has_include`-style specials still need
token identity or their existing special handling.

## Soundness argument

Claim: under the rule, every retention decision the Clang parse produces with
the parse spelling `P` is the decision it would produce with the judge spelling
`J`; hence the accepted model is exactly the one the existing token-identity check
would have accepted had Clang been configured with GCC's spelling.

1. **Preprocessing.** Outside `#`/`##` operands and directives, a pp-number
   token is never inspected for spelling by the preprocessor: it is passed
   through rescanning unchanged and becomes one token of the translation unit
   (phase 7). Rule 4 excludes stringizing, pasting and directives; `#if` is
   excluded by construction (conditions are a separate, already-checked path).
   Token boundaries do not depend on spelling: the replacement is one token in
   both cases, and a following identifier cannot merge with it outside pasting
   (`__SCHAR_MAX___x` is a different identifier, not an expansion).
2. **Conversion to a token (phase 7).** An integer-literal token is converted to
   a prvalue whose only properties are its value and type ([lex.icon]/1-3,
   [expr.prim.literal]). Equal value (rule 2) and equal type (rule 3) make the
   two tokens semantically indistinguishable: overload resolution, template
   argument deduction, `decltype`, `sizeof`, narrowing (value-based for constant
   expressions), constant evaluation, array bounds, and the null pointer
   constant rule ("integer literal with value zero") all depend only on value
   and type. A user-defined literal requires a ud-suffix in the same token,
   which neither definition has, and cannot be formed outside pasting.
3. **The pruning model.** Retention reads the AST (declarations referenced,
   types, associated entities) and the *user text* (`pruningIdentifiers`
   scans `model.text`, which contains the macro name, never the predefine's
   replacement). The literal's spelling appears in neither: Clang's
   `IntegerLiteral` stores an `APInt` value and a `QualType`. So the extracted
   families, edges and system uses are identical for `P` and `J`. The only
   consumer of the replacement spelling is `equalMacro` itself.
4. **Integer model.** Type equality is decided per [lex.icon] with the
   parse target's own widths (what Clang actually used) and additionally the
   judge profile's widths; requiring agreement under both means the decision
   does not depend on which model is right. Whether the parse target models the
   judge's integer widths at all is the pre-existing configuration assumption
   (the parse triple comes from the probe; other width-dependent predefines such
   as `__SIZEOF_LONG__` are still compared token by token when used); this rule
   does not extend it.
5. **Extended integer types.** [lex.icon]/3 allows an implementation-defined
   extended type when no listed type fits; GCC uses `__int128` or "so large that
   it is unsigned", Clang `unsigned long long` with a warning. Such values are
   unclassified and rejected, so no implementation-defined type choice is ever
   equated.
6. **System header chains.** The expansion chain from `CHAR_MAX` to
   `__SCHAR_MAX__` runs through the parse's headers; if the judge's headers used
   a different chain (e.g. stringized it) that difference already exists for
   every system macro and is covered by the pre-existing system-header
   assumption, not introduced here. Within the parse, rule 4's flag covers every
   macro expanded in the span, including system-header ones.

Verdict: **sound** within the stated rule; the relaxation changes only the
disable decision and never a family's retention reason.

## Residual risks / not verified locally

- The `hashOperator` flag and the parse widths are computed in the Clang worker
  (needs the pinned LLVM 23.1.5 SDK); only CI exercises them. The core rule is
  unit tested with hand-built models.
- The span join mirrors the existing paste join (`macroInvocations`); an
  expansion recorded at an argument's physical offset is still inside the
  enclosing invocation's span.
