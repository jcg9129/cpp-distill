<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Evidence extractor fixtures

Fetched directly on **2026-11-17 UTC** with Python urllib, project User-Agent
`cpp-distill-evidence-watch/1 (profile maintenance)` and a 35-second timeout.
All returned HTTP 200 and source content. The production runner uses a
30-second timeout. No challenge was bypassed. Minimal HTML excerpts keep the
original markup, entities, Japanese/Russian text and field whitespace; only
outer closing wrappers are added where needed. URLs below are the requested
URLs (QOJ may redirect to its author path).

| Fixture | Source URL | Retained structure |
| --- | --- | --- |
| [codeforces/current.html](codeforces/current.html) | https://codeforces.com/blog/entry/133046?mobile=false | Announcement title/body; all comments and scripts excluded. |
| [atcoder/current.html](atcoder/current.html) | https://img.atcoder.jp/file/language-update/2025-10/language-list.html | Complete C++23 GCC details element, including nested library table and literal flags script. |
| [cses/current.html](cses/current.html) | https://cses.fi/howto | Language table, with the other languages and C++ dialects retained as distractors. |
| [qoj/current.html](qoj/current.html) | https://qoj.ac/blog/221 | Article through C++ plus next heading; original default article is English, not a translation. |
| [qoj/current-mirror.html](qoj/current-mirror.html) | https://pjudge.ac/blog/221 | Same structural excerpt from the mirror; not independent authentication. |
| [kattis/current.html](kattis/current.html) | https://open.kattis.com/languages/cpp | Language heading and compiler settings section, including displayed/copyable commands. |
| [library_checker/current.toml](library_checker/current.toml) | https://raw.githubusercontent.com/yosupo06/library-checker-judge/master/langs/langs.toml | Complete small langs.toml, including cpp20/cpp17/cpp-func distractors. |
| [yukicoder/current.html](yukicoder/current.html) | https://yukicoder.me/help/environments | OS, table header and ISO/GNU GCC16 rows plus GCC15 distractor; original diagnostics pre block. |
| [homebrew/current.json](homebrew/current.json) | https://formulae.brew.sh/api/formula/gcc.json | Complete small formula object. |
| [docker_hub/current.json](docker_hub/current.json) | https://hub.docker.com/v2/repositories/library/gcc/tags/15.2 | Complete small tag object, with non-amd64 platform distractors. |
| [docker_hub/current-kattis.json](docker_hub/current-kattis.json) | https://hub.docker.com/v2/repositories/kattis/judge-cpp/tags/latest | Complete small tag object. |
| [timus/current.html](timus/current.html) | https://acm.timus.ru/help.aspx?topic=cpp&locale=ru | Russian MinGW paragraph and complete C/GCC/Clang batch block in `help_text`; historic compiler list excluded. |

`fetches.json` records hashes/sizes of the fetched originals before excerpting.
Response cookies and unrelated headers are not retained. The AtCoder recipe
and Timus default English page were also consulted, but are not watched or
saved: their relevant facts are already covered by the retained pages.

Each format directory saves synthetic `changed`, `missing`, `duplicated`,
`challenge`, `login` and empty responses. Changed fixtures replace a selected
version with `99.9.9` (Docker changes the tag name because it does not publish
a compiler version). Missing/duplicated HTML variants remove/repeat just the
selected entry. yukicoder has independent `missing-gnu`/`duplicated-gnu` variants;
its changed version updates both GCC16 rows and the compiler banner. TOML
variants remove/repeat the `cpp` row. Homebrew is a single-object endpoint:
missing removes its formula name, duplicated repeats the `name` JSON key.
Docker removes/repeats the linux/amd64 manifest. Duplicate JSON fields are
rejected, never silently overwritten. Challenge/login pages are synthetic
HTTP-200 substitutions for each format, not fetched successes.

Tests also mutate missing fields, display labels, macro quoting, argument order,
duplicate arguments, line endings/encoding, consent pages and HTTP/transport
failures in memory. The current fixtures and expectations are never derived
from local compiler output. No fixture is fetched during the offline test run.
