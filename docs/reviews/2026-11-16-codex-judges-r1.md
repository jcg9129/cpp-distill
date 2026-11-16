# Codex round 1 on the judge-profile amendment (2026-11-16), cited as *J1-n*

Input: draft amendment (several profiles per judge, live-judge probe) plus §4.3. Verbatim answer:

Assuming “become known facts” means importing snapshot definitions into `initialState()`, **B.5 is unsound**. I would accept observations as diagnostics, not as grounds for stronger resolution.

1. **Blocker — Pasted output does not authenticate execution.** Anyone can paste the shipped expected output; its profile hash and checksum still pass. A checksum detects accidental corruption, not fabrication or deliberate editing. **Fix:** never promote facts from pasted observations; label them “user-reported match.”

2. **Blocker — Equal expansions do not establish equal definitions.** Snapshot `X=Y, Y=1` and judge `X=1, Y=1` produce identical expanded strings. After source code redefines `Y`, their behavior diverges. Real GCC stringizing on both sides does not solve this information loss. **Fix:** require trusted replacement-token definitions to import macro definitions; retain expansion matches only as diagnostics. [GCC stringizing](https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html)

3. **Blocker — Function-like macros receive no definition check.** `F(x)=0`, `F(x)=1`, and object-like `F=1` all pass `#ifdef F`. Special-builtin presence likewise cannot distinguish an active builtin from an ordinary override. **Fix:** presence establishes at most `definedUnknown`, conditional on trusting the environment; never import the snapshot’s signature, replacement, or active-builtin identity from it.

4. **Blocker — Custom tests do not establish grading configuration.** Differences can include flags, wrappers, startup headers, include paths, compiler packages and libraries—not just hardware. One successful run also says nothing about other workers or subsequent updates. **Fix:** require independent evidence connecting the exact grading configuration to the profile; scope observations to their actual environment and date.

5. **Major — Context-dependent expansions cannot become constants.** An ordinary macro can indirectly expand `__LINE__`, `__COUNTER__`, `__FILE__`, `__TIMESTAMP__`, or other contextual builtins. A coincidental match is not a stable definition; changing probe layout or splitting it changes results. **Fix:** exclude such expansions transitively from comparable constant facts; keep active special invocations unknown. [GCC predefined macros](https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html)

6. **Major — The probe disturbs what it measures.** Defining `S`/`S_` changes the macro environment. Expansions can consume counters or execute `_Pragma`, affecting later measurements. “No headers yet” is insufficient. **Fix:** use guarded generated helper names, capture presence before helper definitions, and restrict expansion probes to audited, side-effect-free cases; otherwise report unsupported. [GCC pragmas](https://gcc.gnu.org/onlinedocs/cpp/Pragmas.html)

7. **Major — Arbitrary replacement lists are not safely stringizable arguments.** For example, a macro expanding to an unmatched `)` can break the generated invocation. Variadic helpers handle commas, not every token sequence. Whitespace normalization and escaping also prevent treating output as raw replacement text. **Fix:** support a documented subset; distinguish probe compilation failure from profile mismatch.

8. **Major — Native exclusions are too broad and too shallow.** Ignoring every native-dependent name hides violations of invariant joined facts. Conversely, aliases of native macros may vary without being directly listed. **Fix:** compare observations against the model’s permitted states, check invariant facts, propagate dependencies, and never narrow the model from sampled machines.

9. **Major — Header checks cannot validate A1 inventories.** Header existence and library version macros do not establish header contents or exhaustive reachable macro sets. `<cstddef>`/`<cstdio>` also need not expose every requested vendor version macro. **Fix:** keep these checks diagnostic, use explicit library-specific includes, record the inclusion stage, and regenerate A1 inventories from independently established artifacts.

10. **Major — Negative coverage remains incomplete.** Checking selected names cannot validate A5 or discover additional judge-defined macros. Finding `LOCAL` defined contradicts the assumed environment; it must not merely appear as an optional discrepancy. **Fix:** state coverage explicitly, reject contradicted assumptions, and derive no exhaustive absence claims from a passing probe.

11. **Major — Splitting needs a complete protocol.** Source size alone does not bound output size; expansion can grow considerably. Independently valid parts can come from different runs or configurations. **Fix:** budget source and output bytes, identify each part and its exact fact set, generate corresponding expectations, and reject missing/duplicate/mixed manifests. I have not verified current judge limits.

12. **Major — A mismatch does not uniquely diagnose reproduction failure.** Wrong compiler selection, corrupted output, contextual fields, or probe bugs can cause it. Conversely, downgrading to approximate can retain a contradicted allow-listed fact. **Fix:** classify failures; invalidate affected facts or the profile until investigated, rather than automatically downgrading.

13. **Major — “Newest compiler” is an unsafe moving default.** A release can silently change dialect and platform assumptions while the user still submits under an older judge option. Printing the resolved name afterward does not prevent that. **Fix:** keep compatibility aliases pinned; offer an explicitly moving `latest` alias and support locking the resolved profile digest.

14. **Major — Names and precedence are underspecified.** Family + major + dialect cannot distinguish minor releases, targets, or judge variants. User alias overrides change meaning across machines; an existing user profile named `codeforces` also collides with the proposed bundled alias. **Fix:** use stable judge-option identifiers, explicit bundled/user namespaces, deterministic collision rules, and a documented migration.

15. **Minor — Reporting and schema need stronger identities.** A full name alone cannot distinguish overridden contents. Date-only observation filenames collide; “verification state” overstates a sampled match. **Fix:** update the strict schema and provenance model; report requested selector, resolved origin/path, configuration digest, observation scope/date, and checked/skipped counts. Define hashing without circular inclusion of observations.

16. **Minor — A simpler amendment achieves the useful goals.** Add explicit profiles and a diagnostic probe/check command; leave `initialState()` unchanged. Treat findings as PR evidence for investigating profiles, with manually curated aliases.

Trusted raw macro definitions tied to the actual grading environment would change my answer about promoting ordinary definitions. More matching stringized samples would not.