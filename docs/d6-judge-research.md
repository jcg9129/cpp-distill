<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# D6c judge research notes (2026-11-16/17)

Written by a Sonnet web-research agent; these are claims to re-check against
official pages when each profile is written (see `docs/progress.md`, D6).
CSES, QOJ and Kattis are already shipped; their sections are kept as the
evidence their profiles cite. Profile sketches are proposals, not decisions.


Conventions: "quote" = text returned by the page fetch tool (it truncates long quotes, so a few are paraphrased and say so). UNVERIFIED = inferred, not stated by an official source. "not found" = looked, nothing official. Ranking of reproducibility: reproduced = exact package/image/recipe exists and is obtainable; approximate = same major.minor only.

Cross-cutting finding: the previous research file is correct for Library Checker, Kattis, CSES, Timus. It is WRONG/stale for yukicoder (newest option is now GCC 16.2.0, see below), and for QOJ the claim "reverted from newer after breakage" is not in the blog text I could read (blog says only "Last Updated: June 2026"; no comment mentions a revert).

---------------------------------------------------------------------
## 1. Library Checker (judge.yosupo.jp)

### Sources (official, all fetched)
- https://raw.githubusercontent.com/yosupo06/library-checker-judge/master/langs/langs.toml (current master, fetched 2026-11-16)
- https://raw.githubusercontent.com/yosupo06/library-checker-judge/master/langs/Dockerfile.GCC (current master). Last change to this file (GitHub commits API): 9efb22cd 2026-05-03 "add Dockerfile build checks"; 1bfcd48e 2026-01-02 "upgrade c++" (the 15.2 bump, presumably); 2608ac03 2024-09-03 "upgrade gcc"; a60d83fc 2024-07-15 "upgrade gcc 14.1 and add C++23".
- https://raw.githubusercontent.com/yosupo06/library-checker-judge/master/README.md ("Ubuntu 22.04 (Judge Server)" host)
- https://raw.githubusercontent.com/yosupo06/library-checker-judge/master/restapi/handlers_submissions.go
- https://hub.docker.com/v2/repositories/library/gcc/tags/15.2 and https://raw.githubusercontent.com/docker-library/gcc/master/15/Dockerfile

### 1. Option, flags
Labels (langs.toml): `name = "C++23"`, `version = "GCC 15.2 + AC Library(1.6)"`, id `cpp` (also `cpp20`, `cpp17`, and `cpp-func` named "C++23(Function)"). Verbatim entry:

    [[langs]]
        id = "cpp"
        name = "C++23"
        version = "GCC 15.2 + AC Library(1.6)"
        source = "main.cpp"
        image_name = "library-checker-images-gcc"
        compile = [
            "g++", "-O2", "-std=c++23", "-DEVAL", "-DONLINE_JUDGE", "-march=native",
            "-o", "main", "main.cpp", "-I", "/opt/ac-library"
        ]
        exec = ["./main"]

- Compile flags: `-O2 -std=c++23 -DEVAL -DONLINE_JUDGE -march=native -I /opt/ac-library`. Link-only flags: none (no -l, no -static). -D macros: EVAL, ONLINE_JUDGE. -march=native (depends on the judge CPU, which is not published: not found).
- cpp-func variant: same flags, sources `grader.cpp main.cpp`, additional files fastio.h grader.cpp solve.hpp.
- Library: AC Library v1.6 copied to /opt/ac-library/atcoder (Dockerfile: `git clone https://github.com/atcoder/ac-library/ -b v1.6`). No Boost/testlib on the include path.
- GCC exact version: label says "15.2". Docker tag `gcc:15.2` = GCC 15.2.0 (docker-library build; patch level 0 UNVERIFIED because the tag name only says 15.2, but docker-library tags `15.2` for 15.2.0).

### 2. OS / arch / libc
- Compiler runs inside Docker image `FROM gcc:15.2` (not the host). Judge host OS: README "Ubuntu 22.04 (Judge Server)", irrelevant to compiler ABI.
- gcc official image is Debian based (`FROM buildpack-deps:<suite>`; current master Dockerfile for 15.x says `buildpack-deps:trixie`; the suite used by the `15.2` tag itself is UNVERIFIED, probably trixie). glibc therefore Debian's (trixie 2.41, UNVERIFIED).
- Arch: amd64 (the judge uses the x86_64-unknown-linux-musl init binary: Dockerfile line `cargo build --release --target=x86_64-unknown-linux-musl`).
- Triple: GCC in the docker-library image is configured `--build="$gnuArch" --disable-multilib --enable-languages=c,c++,fortran,go`; gnuArch normally `x86_64-linux-gnu`, so `-dumpmachine` = x86_64-linux-gnu (UNVERIFIED, from the Docker recipe, not from the judge).

### 3. Provenance
- Docker official image `gcc:15.2`. Index digest at 2026-11-16 (Docker Hub API, last_updated 2026-06-11T17:19:08Z): `sha256:3ae15afe768b06d0c0fe088d822ba5f8045c26630bdacc8d8e7713cf5d8e7289`; amd64 manifest `sha256:c101370f78e4a30be178c11dd18aeee64c65d617908a98157db2392ca73ab04f`.
- Caveat: the judge's Dockerfile pins only the tag (not a digest), so the judge's actual image may differ from today's digest if the tag was rebuilt for base updates. The compiler itself (GCC 15.2.0 from source, `--build`, default specs) does not change between rebuilds of the tag, so predefined macros will be identical. docker-library master has already moved to 15.3.0, so tag `15.2` is a frozen older line; it may disappear from Docker Hub eventually (obstacle, low risk now).
- Verdict: REPRODUCED BUILD (exact published Dockerfile + official image tag; compiler is source-built identically).

### 4. CI acquisition
`docker pull gcc:15.2` (or by digest above) on ubuntu-latest, then `docker run gcc:15.2 g++ -dM -E -x c++ ... ` with the flags above. For `-I /opt/ac-library` clone `https://github.com/atcoder/ac-library` tag v1.6 (macros do not depend on it, but ACL defines nothing in predefined set). -march=native macros are not reproducible: runner CPU differs from the judge's (blocking for exact __AVX2__/__BMI2__/etc.; must be listed "inert"/documented or the snapshot taken with a documented caveat).

### 5. Limits / custom test
- Source size limit: `if len(body.Source) == 0 || len(body.Source) > 1024*1024 {` -> "invalid source length" (restapi/handlers_submissions.go, PostSubmit): 1 MiB.
- Output limit: not found (set per problem in library-checker-problems, not read).
- Custom test: none (problem-set site; submission is the only way to run code). Compile errors are shown to the submitter, so a probe via submission to any problem would work only on a real problem, not as a free-form tool.

### Profile sketch
- name: `library-checker-gcc15-cpp23`
- triple: x86_64-linux-gnu (UNVERIFIED)
- compiler: gcc 15.2.0 (patch UNVERIFIED but docker tag 15.2 = 15.2.0)
- std: c++23
- flags: ["-O2", "-DEVAL", "-DONLINE_JUDGE", "-march=native", "-I/opt/ac-library"] (note -march=native problem; -I is a path flag, ACL v1.6 needed in snapshot environment)
- link: []
- libraries: ["libstdc++", "glibc", "ac-library-1.6"]
- reproduced: yes (gcc:15.2 official Docker image, digest above)
- CI: docker pull gcc:15.2 on ubuntu runner; clone ac-library v1.6 to /opt/ac-library.
- Blocking gap: none for compiler; -march=native macro set is runner-dependent.

---------------------------------------------------------------------
## 2. yukicoder

### Sources (official, fetched 2026-11-16)
- https://yukicoder.me/help/environments ("実行環境"). Page states its command listings and /proc/cpuinfo reflect 2026-11-14 11:08:09.
- https://yukicoder.me/submissions/1164458 (submission 2026-05-12, judged 2026-09-25: label "C++23 (gcc 15.3.0 + boost 1.92.0 + ACL)")
- https://yukicoder.me/submissions/684931 (No.9000 Hello World test problem, 2021, old label shown; same command with g++-15 because submissions are re-judged)
- https://yukicoder.me/help (limits), https://yukicoder.me/run (online run)
- https://formulae.brew.sh/api/formula/gcc.json, gcc@15.json, raw Homebrew gcc.rb

### 1. Options (newest first)
Rows on /help/environments (paraphrased table; commands quoted):
- `cpp23_gcc16` "C++23 (gcc16)": GCC 16.2.0, Boost 1.92.0, ACL (original ac-library, placed under /usr/include/)
- `cpp23_gnu_gcc16` "C++23 (GNU extensions, gcc16)": same, gnu++23
- `cpp23` / `cpp23_gnu`: GCC 15.3.0 (what the older submission label shows)
- also cpp17(+gnu) gcc15, cpp17_gcc12 (12.4.0), cpp17 clang (clang++ 23.1.4), cpp14, cpp11 etc.

Newest GCC option compile line (fetch tool returned it split in two; join with one space):

    g++-16 -O2 -fplugin=/usr/lib/yukicoder/noavx512-gcc16.so -lm -std=c++23 -Wuninitialized -DONLINE_JUDGE -o a.out _filename_

gnu variant: identical except `-std=gnu++23`. Run: `./a.out`. GCC 15 equivalent (verbatim from submission 1164458):

    g++-15 -O2 -fplugin=/usr/lib/yukicoder/noavx512-gcc15.so -lm -std=c++23 -Wuninitialized -DONLINE_JUDGE -o a.out _filename_

- Preprocess/compile flags: `-O2 -fplugin=/usr/lib/yukicoder/noavx512-gcc16.so -std=c++23 -Wuninitialized -DONLINE_JUDGE`. Link: `-lm` (appears before the source, positional). No -I (library paths: ACL under /usr/include, Boost headers /usr/local/include/boost). No -march/-mtune. -D: ONLINE_JUDGE only.
- The `-fplugin` is a yukicoder-private GCC plugin (name "noavx512"); it is not published (not found). It presumably disables AVX-512 target features; effect on predefined macros UNVERIFIED (probably none without -march). It cannot be loaded in a reproduction: obstacle, but should not change `-dM` output.
- Version banner (help page, 各コンパイラバージョン): "g++-16 (Homebrew GCC 16.2.0) 16.2.0", "g++-15 (Homebrew GCC 15.3.0) 15.3.0", "g++-12 (Ubuntu 12.4.0-5ubuntu1) 12.4.0".

### 2. OS / arch
- "実行OS環境": "Ubuntu 25.04 (Plucky)". Ubuntu 25.04 reached EOL in January 2026 (from general knowledge; the page still says so).
- Arch: x86_64 (the page's /proc/cpuinfo shows an Intel Xeon 2.60GHz flags line; the same page elsewhere mentions AMD EPYC Milan Zen 3; inconsistent, as the fetch tool reported).
- GCC is Homebrew (Linuxbrew) GCC, NOT the Ubuntu package. Homebrew gcc.rb: Linux configure has no `--build`, so config.guess gives `x86_64-pc-linux-gnu` (UNVERIFIED, derived from the formula), `--disable-multilib`, `--enable-default-pie`, `--with-linker-hash-style=gnu`, `--with-pkgversion="Homebrew GCC #{pkg_version}"`, `--program-suffix=-16`, `--with-system-zlib`. glibc = Ubuntu 25.04 system glibc 2.41 (UNVERIFIED, from distro knowledge).

### 3. Provenance
- Homebrew formula `gcc` (alias gcc@16, gfortran), homebrew/core Formula/g/gcc.rb: stable 16.2.0, revision 0, bottle rebuild 1; x86_64_linux bottle sha256 `6504ecae947061ffe79c9dea2f635791bf54b148d1063ec857873eac2a67d922`. Source tarball `https://ftpmirror.gnu.org/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz` sha256 `e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e`.
- `gcc@15`: 15.3.0, bottle rebuild 1, x86_64_linux sha256 `926b74086c0e13dc7cfa9002d6d005ccc5cbebd8b64546d278f4ccae90f32b61`.
- Note the Homebrew API describes TODAY's bottle; yukicoder's installed bottle could have a different rebuild (UNVERIFIED), but compiler 16.2.0 sources are identical.
- Verdict: REPRODUCED BUILD (Homebrew bottle with checksum; version string matches "Homebrew GCC 16.2.0"), except the private plugin.

### 4. CI acquisition
Linuxbrew is preinstalled on GitHub ubuntu runner images (UNVERIFIED; if absent install Homebrew via official script, or the `Homebrew/actions/setup-homebrew` action). `brew install gcc` (gets 16.2.0 only while it is the stable formula; newer Homebrew gcc releases will replace it: pin by bottle sha / use `brew fetch --bottle-tag=x86_64_linux gcc` or install from a tap commit). Alternatively build from the tarball with the configure args above (slow). Obstacle: Homebrew bottles for old versions disappear when superseded; GCC 15.3.0 via `gcc@15` is stable for now. Obstacle 2: noavx512 plugin not available.

### 5. Limits / custom test
- Source size: "最大128KB (131,072バイト)に制限しています。" (https://yukicoder.me/help)
- Output: maximum 20 MB (actual limit slightly lower), stderr counts, OLE on excess (same page).
- Custom run: yes. https://yukicoder.me/run ("オンライン実行"): choose language, enter source and stdin, can edit compile/run commands; login required ("ログインしている場合のみ実行できます。"). Also test problem No.9000. Compile line visible in submission detail.

### Profile sketch
- name: `yukicoder-gcc16-cpp23`  (also optionally `yukicoder-gcc16-gnupp23`; keep gcc15 variant `yukicoder-gcc15-cpp23` for 15.3.0)
- triple: x86_64-pc-linux-gnu (UNVERIFIED)
- compiler: gcc 16.2.0
- std: c++23
- flags: ["-O2", "-Wuninitialized", "-DONLINE_JUDGE"]; plugin option `-fplugin=...noavx512...` omitted and documented as inert/unavailable
- link: ["-lm"]
- libraries: ["libstdc++", "glibc", "boost-1.92.0", "ac-library"] (ACL version not stated: not found)
- reproduced: yes for compiler (Homebrew bottle checksum); plugin unverifiable
- CI: brew install gcc on Linuxbrew, or from source tarball sha256 above.
- Blocking gap: none for compiler; plugin effect UNVERIFIED.

---------------------------------------------------------------------
## 3. Luogu (luogu.com.cn)

### Sources (fetched 2026-11-16)
- https://help.luogu.com.cn/manual/luogu/problem/judging (undated; footer "Copyright (C) 2026")
- https://github.com/luogu-dev/judge-env: flake.nix, flake.lock, gcc/overlay.nix, commit list (API)

### 1. Options and flags
- Supported standards (page): C++98, C++11, C++14, C++17, C++20, C++23 ("C++23 (ISO/IEC 14882:2024)").
- Page: current GCC "gcc (GCC) 15.3.0"; separate NOI-compatible option: GCC 9.3.0 with C++14 only ("我们专门提供了 GCC 9.3.0 版本的 C++14 标准选项").
- Verbatim compile line: `g++ -x c++ -std=$version -fPIC -DONLINE_JUDGE -Wall -fno-asm -lm -march=native`, and "O2 优化开关为 -O2" (the -O2 added when the O2 switch is on).
- Preprocess/compile: `-x c++ -std=<ver> -fPIC -DONLINE_JUDGE -Wall -fno-asm [-O2] -march=native`; link: `-lm`. -fPIC and -march=native are notable (-march=native is judge-CPU dependent; CPU not published: not found). Note `-fno-asm` makes `asm` a non-keyword (macros unaffected but `__STRICT_ANSI__`? not touched; UNVERIFIED).
- Which `-std=` for "C++23": probably `c++23` vs gnu++23 not stated (the page says `$version`). UNVERIFIED.
- Libraries: testlib header added via `-idirafter ${testlib}/include` in the wrapper libc-cflags (overlay.nix); no ACL/Boost stated.

### 2. OS
- "洛谷评测系统运行于 Linux 上，虽与主流竞赛相同，但无法保证 Linux 内核版本一致。" Distribution not stated; the toolchain comes from Nix, so libc = nixpkgs glibc at the locked nixpkgs rev (version UNVERIFIED). Arch: flake `system = "x86_64-linux"`.
- Triple: x86_64-unknown-linux-gnu (nixpkgs convention; UNVERIFIED).

### 3. Provenance
- Flake https://github.com/luogu-dev/judge-env (master). Head commit 508d2b52 2026-10-06 "update dependencies". flake.lock: `nixpkgs` = NixOS/nixpkgs rev `43629f368cad6fe7e1c4b7ede3d73a8d1a9fcaed`, narHash `sha256-TiHMCqxkgIVIxl51wslcDY5DCmFTAzARTrct6owjTEA=`, lastModified 2026-10-06 08:45:29, ref nixos-unstable-small. `nixpkgs_gcc930` = rev `99cd95772761842712f77c291d26443ee039d862` (2022-06-05).
- gcc/overlay.nix: `luogu-gcc = applyLuogu gcc15 ljudge-glibcLocales [...]`, `luogu-gcc930 = applyLuogu gcc930 ...`; patches `13_disable-pragma-and-attribute-for-optimize.patch` / `9_...`: the README says Luogu patched GCC to disable enabling O2 via `#pragma` and `__attribute__((optimize))`. So the judge GCC is a PATCHED nixpkgs gcc15; GCC patch level 15.3.0 per the page (the nixpkgs rev's gcc15 version was not read: UNVERIFIED but consistent).
- Verdict: REPRODUCED BUILD possible via `nix build` of the flake at the lock rev (exact recipe) but this is a custom patched compiler; predefined macros are almost certainly identical to vanilla 15.3.0, but pragma/attribute behavior differs. The flake HEAD may have moved past the version the live site runs (page 15.3.0 vs flake lock date 2026-10-06; match UNVERIFIED). Classify as "reproduced (recipe published)", with the caveat above.

### 4. CI acquisition
ubuntu runner + `cachix/install-nix-action`, then `nix shell github:luogu-dev/judge-env/<commit>#gcc -c g++ -dM -E ...`. Cachix cache `luogu-judge` (README: `cachix use luogu-judge`) avoids rebuilding GCC (without it the patched GCC build is large/slow; Actions 6h limit may be tight, and the VM disk is limited). Obstacle: nix store disk; patched build time.

### 5. Limits / custom test
- Source size limit: not found on the official judging page (explicit search: no 50KB statement). Output limit: page only defines OLE "输出超过限制", no number (not found).
- Custom test: https://www.luogu.com.cn/ide returned only a title ("Welcome - Luogu Spilopelia"); functionality NOT verified (UNVERIFIED that a free-form run feature exists). help sidebar lists no custom-test page.

### Profile sketch
- name: `luogu-gcc15-cpp23` (std value UNVERIFIED c++23 vs gnu++23; if gnu, `luogu-gcc15-gnupp23`)
- triple: x86_64-unknown-linux-gnu (UNVERIFIED)
- compiler: gcc 15.3.0
- std: c++23
- flags: ["-fPIC", "-DONLINE_JUDGE", "-Wall", "-fno-asm", "-O2", "-march=native"]  (-x c++ implicit)
- link: ["-lm"]
- libraries: ["libstdc++", "glibc", "testlib"]
- reproduced: recipe-reproducible (nix flake lock) but patched GCC; call it "reproduced build with patch note"
- CI: nix + cachix luogu-judge, `nix shell github:luogu-dev/judge-env/508d2b5#gcc`
- Blocking gap: whether the live judge runs the HEAD flake rev; -march=native; no probe channel confirmed.

---------------------------------------------------------------------
## 4. Kattis (open.kattis.com)

### Sources (fetched 2026-11-16)
- https://open.kattis.com/languages/cpp (live page, undated)
- https://open.kattis.com/languages (index only: lists C++ slug `cpp`)
- https://hub.docker.com/v2/repositories/kattis/judge-cpp/ and /tags/latest (Docker Hub, namespace kattis)
- Searched: open.kattis.com/info/submit, support.kattis.com (limits not found)

### 1. Option
Quote: "We use `g++-15 15.2.0` to compile your program with the following command:"

    g++-15 -g -O2 -o {executable} -std=gnu++23 -static -lrt -Wl,-u,_IO_wfile_doallocate -Wl,--whole-archive -lpthread -Wl,--no-whole-archive {files}

- Compile: `-g -O2 -std=gnu++23`. Link: `-static -lrt -Wl,-u,_IO_wfile_doallocate -Wl,--whole-archive -lpthread -Wl,--no-whole-archive`. No -D macros (no ONLINE_JUDGE), no -march/-mtune, no -I, no libraries. Extensions: .cpp .cc .C .cxx .c++. Label: "C++" (slug cpp).
- Page says nothing about OS, libraries, or multiple C++ options.

### 2. OS / libc
- Not stated. Static link flags (-lrt, whole-archive pthread, `_IO_wfile_doallocate`) imply static glibc (glibc 2.3x workaround). Version-wise, g++-15 15.2.0 exists in Debian trixie, Ubuntu 25.10 (15.2.0-4ubuntu4) and Ubuntu 26.04 (15.2.0-16ubuntu1; Launchpad); distribution UNVERIFIED. Arch: x86_64 assumed (Docker Hub kattis/judge-cpp is amd64/linux only). Triple: x86_64-linux-gnu (UNVERIFIED).

### 3. Provenance
- Kattis publishes Docker Hub images `kattis/judge-cpp` (latest digest `sha256:453998e7fefcf47fb86c094be4718f2986c0af3e7a079c7bd5696a60929d514d`, pushed 2026-11-02, amd64/linux 259.7 MB, pulled 2026-11-08, updater user "gkreitz"; older dated tags 2023-11 to 2024-07 (those are 700MB-1GB, so a different composition)). Docker Hub gives no description, no Dockerfile; I could not confirm that it contains g++-15 or is the production image (UNVERIFIED; plausible given date and name).
- Verdict: APPROXIMATE (page gives only 15.2.0 and the command; no package revision). An exact Debian/Ubuntu revision can be guessed but not confirmed. `kattis/judge-cpp:latest` is a candidate for an exact build but UNVERIFIED and I could not inspect layers (no pulling allowed).

### 4. CI acquisition
Option A (approximate): Debian trixie container `debian:trixie` + `apt-get install g++-15` (15.2.0-x), or Ubuntu 26.04 container g++-15 15.2.0-16ubuntu1. Option B (maybe exact, to verify later): `docker pull kattis/judge-cpp@sha256:4539...514d` and `g++-15 --version` / `-dumpmachine`. Obstacle: Kattis tags are mutable (`latest`); only ~31 tags.

### 5. Limits / custom test
- Source size limit, output limit: not found on any Kattis page I could read (a "128 KB" figure is NOT confirmed).
- Custom test: none known. support.kattis.com has "Can I test my solution before I submit?" (samples only), https://support.kattis.com/support/solutions/articles/79000122495-can-i-test-my-solution-before-i-submit- (title only; content not read).

### Profile sketch
- name: `kattis-gcc15-gnupp23`
- triple: x86_64-linux-gnu (UNVERIFIED)
- compiler: gcc 15.2.0
- std: gnu++23
- flags: ["-g", "-O2"]
- link: ["-static", "-lrt", "-Wl,-u,_IO_wfile_doallocate", "-Wl,--whole-archive", "-lpthread", "-Wl,--no-whole-archive"]
- libraries: ["libstdc++", "glibc"]
- reproduced: no, approximate (version only)
- CI: `debian:trixie` or `ubuntu:26.04` container with g++-15; or try `kattis/judge-cpp` image.
- Blocking gap: OS/libc unstated; no limits; no probe channel.

---------------------------------------------------------------------
## 5. CSES (cses.fi)

### Sources (fetched 2026-11-16)
- https://cses.fi/howto (no date or "last updated" on page)

### 1. Option
Quote: "g++ 13.3.0" (C++). Flags quoted: "-std=c++11 -O2 -Wall", "-std=c++17 -O2 -Wall", "-std=c++20 -O2 -Wall", "-std=c++23 -O2 -Wall". Section headings: "How to use CSES", "Languages", "Example programs". The page lists no full command line, no -o, no link flags, no -D (no ONLINE_JUDGE), no -march, no libraries. Label shown in the submit form: not read (page does not give one).

### 2. OS
- Not stated (page says nothing on OS, arch or 64-bit). Inference UNVERIFIED but strong: the same page lists Python 3.12.3, Node.js 18.19.1, ruby 3.2.3, rustc 1.75.0, make 4.3, FPC 3.2.2, NASM 2.16.01 = Ubuntu 24.04 package versions, and Ubuntu 24.04 g++-13 = 13.3.0-6ubuntu2~24.04.1 (Launchpad: updates 2026-02-16, security 2026-02-26; earlier 13.3.0-6ubuntu2~24.04 from 2024-12). glibc 2.39. Triple x86_64-linux-gnu (Ubuntu convention, UNVERIFIED).

### 3. Provenance
- Ubuntu 24.04 apt `g++-13`. Package revision unknown: 13.3.0-6ubuntu2~24.04 or ~24.04.1 are both 13.3.0, identical compiler behavior for macros. Verdict: APPROXIMATE strictly (revision not published), practically near-exact.

### 4. CI acquisition
GitHub `ubuntu-24.04` runner (apt `g++-13` 13.3.0-6ubuntu2~24.04.x; ubuntu-24.04 image already has gcc-13) or `ubuntu:24.04` container.

### 5. Limits
- Source: "The source code size limit is 128 kB for all languages." Output limit: not found. Custom test: not found (page mentions none).

### Profile sketch
- name: `cses-gcc13-cpp23`
- triple: x86_64-linux-gnu (UNVERIFIED)
- compiler: gcc 13.3.0
- std: c++23
- flags: ["-O2", "-Wall"]
- link: []
- libraries: ["libstdc++", "glibc"]
- reproduced: approximate (distro revision not stated, but Ubuntu 24.04 inference strong)
- CI: ubuntu-24.04 runner, apt g++-13 (13.3.0-6ubuntu2~24.04.1)
- Blocking gap: none material (OS not stated; no -DONLINE_JUDGE).

---------------------------------------------------------------------
## 6. QOJ (qoj.ac; pjudge.ac mirror)

### Sources
- https://pjudge.ac/blog/221 "Judging environment of QOJ" (fetched 2026-11-17; posted 2021-03-23 by Qingyu, "_Last Updated: June 2026_"). https://qoj.ac/blog/221, qoj.ac and jiang.ly problem pages all return 403; the pjudge.ac mirror is the source used. Mirror of the official blog (same author/system), not qoj.ac itself.
- Launchpad API (gcc-15 sources) and packages.ubuntu.com for Ubuntu resolute (26.04)

### 1. Option
- C++ (GCC 15.2.0): `g++ -o answer -x c++ answer.code -lm -O2 -DONLINE_JUDGE -std=c++XX` with XX in 98, 11, 14, 17, 20, 23, 26 (paraphrased single line; matches the earlier research text).
- Compile: `-x c++ -O2 -DONLINE_JUDGE -std=c++23` (or `c++26`; newest standard offered is c++26). Link: `-lm`. No -march, no -I, no libraries stated. Option label shown to users on QOJ submission form: not found (blog uses "C++").
- The blog's C line: "gcc -o answer -x c answer.code -lm -O2 -DONLINE_JUDGE -std=cXX" (99, 11). Fortran line says "GNU Fortran 15.0.1" (odd; possibly a version quirk; UNVERIFIED relevance).
- Specs: judgehosts "Ubuntu 26.04.1 LTS", RAM 8 GiB, "Intel(R) Xeon(R) Platinum 8573C" (Azure; kernel not listed).

### 2. OS / arch / libc
- Ubuntu 26.04.1 LTS (resolute), amd64 (Xeon). libc6 2.43-2ubuntu2.4 on resolute amd64 (packages.ubuntu.com, 2026-11-16). Triple x86_64-linux-gnu (UNVERIFIED).

### 3. Provenance
- Blog does not say how GCC was installed. Ubuntu 26.04 `g++-15` = `15.2.0-16ubuntu1` (Launchpad: resolute Release 2026-03-31; newer gcc-15 15.3.0-4ubuntu1 exists only in stonking/tenacious, i.e. 26.10+). So "GCC 15.2.0" on Ubuntu 26.04.1 exactly matches the stock apt package (inference UNVERIFIED but consistent; no resolute-updates SRU for gcc-15 seen).
- The earlier claim "reverted from newer after breakage" is not supported by the blog text I read (comments do not mention it); treat as UNVERIFIED.
- Verdict: probable reproduced build (Ubuntu 26.04 g++-15 15.2.0-16ubuntu1), labelled approximate in strictness because the blog gives no package revision.

### 4. CI acquisition
ubuntu-latest runner is 24.04 (no resolute packages). Use a job container `ubuntu:26.04` (official Docker image exists since 2026-04) then `apt-get install g++-15=15.2.0-16ubuntu1` (also provides the same libc6 2.43). Obstacle: container jobs use the runner kernel; irrelevant for macros. If ubuntu-26.04 hosted runners exist, use those (not checked).

### 5. Limits / custom test
- Platform-wide source limit: not found (qoj.ac blocked). Problem-specific limits exist (e.g. a problem sets 102400 bytes; Universal Cup contest blog mentions a 200 KiB file limit): these are contest/problem rules, not general. UOJ upstream default 64 KB is UNVERIFIED for QOJ.
- Output limit: not found.
- Custom test: search results show a "Custom Test" option on QOJ problem pages (problem #5508 per search snippet; page fetch returned 403, so limits and behavior not verified). UNVERIFIED.

### Profile sketch
- name: `qoj-gcc15-cpp23` (QOJ also offers c++26: could add `qoj-gcc15-cpp26`)
- triple: x86_64-linux-gnu (UNVERIFIED)
- compiler: gcc 15.2.0
- std: c++23
- flags: ["-O2", "-DONLINE_JUDGE"]
- link: ["-lm"]
- libraries: ["libstdc++", "glibc"]
- reproduced: yes-likely (Ubuntu 26.04 g++-15 15.2.0-16ubuntu1), strict approximate because package not stated
- CI: `container: ubuntu:26.04`, apt g++-15
- Blocking gap: official qoj.ac pages 403 (only mirror); provenance and limits unverified.

---------------------------------------------------------------------
## 7. Timus (acm.timus.ru)

### Sources (fetched 2026-11-16)
- https://acm.timus.ru/help.aspx?topic=cpp (compiler list and end-of-use history; the last dated entry in the list: VC++ 2019/MinGW GCC 9.2/Clang 10.0.0 "used until January, 22 2024", so the current set dates from 2024-01-22)
- https://acm.timus.ru/help.aspx?topic=judge&locale=en (limits), https://acm.timus.ru/help.aspx?topic=faq
- https://winlibs.com/ and the WinLibs release pages via search

### 1. Option
Current compilers: "Microsoft Visual C++ 2022 17.8", "GCC 13.2.0", "Clang 17.0.6"; "All compilers support at least C17 and C++20 standards." GCC C++ line as written on the page:

    g++ -x c++ -std=c++23 -O2 -fno-strict-aliasing -march=native -DONLINE_JUDGE -static -Wl,-s,--stack=67108864 -o %1.exe %1

(the earlier research file wrote `gcc -x c++`; this fetch says `g++`; the page wraps the line after -march=native.)
- Compile: `-x c++ -std=c++23 -O2 -fno-strict-aliasing -march=native -DONLINE_JUDGE`. Link: `-static -Wl,-s,--stack=67108864`. -march=native depends on the judge CPU (not published: not found).
- Quote: "MinGW GCC 13.2.0 x64 and Clang 17.0.6 x64 are taken from the build provided by WinLibs." (link https://winlibs.com/). Release name, CRT (msvcrt/ucrt) and thread model are NOT stated.
- Note: "ONLINE_JUDGE macro can be used..." (page text).

### 2. OS / arch / runtime
- Windows x64 (inferred from .exe and WinLibs, UNVERIFIED version). Triple: x86_64-w64-mingw32 (WinLibs standard; UNVERIFIED for the judge). WinLibs x64 builds are `seh`-exception, and `posix` or `mcf` or `win32` threads; the GCC 13.2.0 + LLVM 17.0.6 WinLibs builds are POSIX threads (from release names `13.2.0posix-17.0.6-11.0.1-ucrt-r5` / `msvcrt-r5`), CRT UCRT or MSVCRT undetermined (blocking for `_UCRT`/`__MSVCRT__`-style macros).

### 3. Provenance
- WinLibs release containing both GCC 13.2.0 and LLVM 17.0.6 + MinGW-w64 11.0.1: tags `13.2.0posix-17.0.6-11.0.1-ucrt-r4`, `-ucrt-r5`, `-msvcrt-r3`, `-msvcrt-r4`, `-msvcrt-r5` (search results, newreleases.io mirror of https://github.com/brechtsanders/winlibs_mingw/releases). Timus changed on 2024-01-22: ucrt-r4 was packaged 2024-01-15, msvcrt-r4 2023-12-26, msvcrt-r5 2024-02-03 (after the change); so a good guess is r4 (UCRT or MSVCRT unknown). UNVERIFIED.
- Verdict: APPROXIMATE. Cannot pin; macros of ucrt vs msvcrt differ (`_UCRT` defined by mingw-w64 headers under UCRT builds; MSVCRT builds define `__MSVCRT__`; both vanilla GCC macros equal otherwise).
- Download URL pattern (each release has checksums on the GitHub release page; asset digests not read): `https://github.com/brechtsanders/winlibs_mingw/releases/download/13.2.0posix-17.0.6-11.0.1-ucrt-r4/winlibs-x86_64-posix-seh-gcc-13.2.0-llvm-17.0.6-mingw-w64ucrt-11.0.1-r4.7z` (name by analogy with the verified r8 naming on winlibs.com; r4 filename UNVERIFIED).

### 4. CI acquisition
windows-latest runner: download the 7z/zip from GitHub releases, verify sha256 from the release page, put `mingw64\bin` on PATH. Obstacle: UCRT/MSVCRT undecided; the ucrt-r4 vs msvcrt-r4 should be produced as two snapshots, or documented as inert. Alternative for an approximate GCC 13.2.0: MSYS2 mingw-w64 package (not same build).

### 5. Limits / custom test
- Source size limit: not found (judge help page doesn't mention one). Output: "the output limit is set with a large margin and is usually not specified in the problem statement" (judge help).
- Custom test: none known (FAQ: "Test sets are kept in secret. Users are advised to write their own tests").

### Profile sketch
- name: `timus-gcc13-cpp23`
- triple: x86_64-w64-mingw32 (UNVERIFIED)
- compiler: gcc 13.2.0
- std: c++23
- flags: ["-O2", "-fno-strict-aliasing", "-march=native", "-DONLINE_JUDGE"]
- link: ["-static", "-Wl,-s,--stack=67108864"]
- libraries: ["mingw-w64", "libstdc++"]
- reproduced: no (approximate): WinLibs release not stated; candidate `13.2.0posix-17.0.6-11.0.1-{ucrt,msvcrt}-r4`
- CI: windows-latest, download WinLibs candidate zip, verify checksum
- Blocking gap: CRT flavour (UCRT vs MSVCRT) and release unknown; -march=native.

---------------------------------------------------------------------
## Summary table of blocking gaps
| Judge | Reproduced? | Blocking gaps |
|---|---|---|
| Library Checker | yes (gcc:15.2 image) | -march=native runner-dependent; tag digest may differ from the judge's pull |
| yukicoder | yes (Homebrew bottle) | private -fplugin; Homebrew gcc 16 may be superseded; no stated -std for "gnu" fine |
| Luogu | recipe yes (patched nix flake) | live rev vs flake HEAD; std (c++ vs gnu++) literal; -march=native; limits not found |
| Kattis | approximate | OS/distro unstated; limits not found; no probe |
| CSES | approximate (near exact) | OS unstated (Ubuntu 24.04 inferred); output limit not found |
| QOJ | likely yes (Ubuntu 26.04 apt) | qoj.ac 403, only mirror; provenance not stated; limits not found |
| Timus | approximate | WinLibs release and CRT (ucrt/msvcrt) unknown; -march=native |
