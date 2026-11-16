<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Third-party notices

Release binaries contain statically linked LLVM/Clang libraries and embedded
Clang resource headers from **llvmorg-23.1.5**, using SDK **llvm-23.1.5-1**.
The SDK uses the unmodified `llvm-project-23.1.5.src.tar.xz` release source.
`BUILDINFO.txt` identifies its checksum, the platform SDK checksum, the consumer
compiler and its flags. Release notes link the matching source archives.

| Component | License | Source / scope |
| --- | --- | --- |
| LLVM and Clang (frontend, parser, AST, Sema, lexer, Basic and transitive LLVM support/target libraries) | Apache-2.0 WITH LLVM-exception; legacy portions under University of Illinois/NCSA | [llvm-project llvmorg-23.1.5](https://github.com/llvm/llvm-project/tree/llvmorg-23.1.5); complete upstream text in `LLVM-LICENSE.txt` |
| Clang resource headers, including architecture intrinsics | Apache-2.0 WITH LLVM-exception (individual notices retained in embedded bytes) | [clang/lib/Headers](https://github.com/llvm/llvm-project/tree/llvmorg-23.1.5/clang/lib/Headers) |
| xxHash by Yann Collet, adapted in LLVM Support | BSD-2-Clause | [llvm/lib/Support/xxhash.cpp](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/lib/Support/xxhash.cpp); notice below |
| BLAKE3 by the BLAKE3 contributors | Apache-2.0 OR CC0-1.0; distributed here under Apache-2.0 | [llvm/lib/Support/BLAKE3](https://github.com/llvm/llvm-project/tree/llvmorg-23.1.5/llvm/lib/Support/BLAKE3); Apache text in `LLVM-LICENSE.txt` |
| SipHash, adapted from Jean-Philippe Aumasson and Daniel J. Bernstein | Apache-2.0 WITH LLVM-exception for LLVM's adaptation | [third-party/siphash](https://github.com/llvm/llvm-project/tree/llvmorg-23.1.5/third-party/siphash) |
| Unicode UTF conversion code, Unicode, Inc. | Unicode-DFS-2015 notice | [ConvertUTF.h](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/include/llvm/Support/ConvertUTF.h); notice below |
| Regular-expression implementation by Henry Spencer, with Berkeley/OpenBSD modifications | Spencer license and BSD-3-Clause | [COPYRIGHT.regex](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/lib/Support/COPYRIGHT.regex); upstream adapted version, notice below |
| MD5 by Alexander Peslyak (Solar Designer) | Public domain, with permissive fallback | [MD5.cpp](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/lib/Support/MD5.cpp); notice below |
| SHA-256 based on NaCl public-domain code | Public domain origins; LLVM adaptation Apache-2.0 WITH LLVM-exception | [SHA256.cpp](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/lib/Support/SHA256.cpp) |
| Linux: GCC libstdc++ and libgcc, including template/header code | GPL-3.0-or-later WITH GCC-exception-3.1 | [GCC source releases](https://gcc.gnu.org/releases.html), [libstdc++ license](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html); static `-static-libstdc++ -static-libgcc`, GCC Runtime Library Exception below |
| macOS: libc++ and libc++abi, including compiled template/header code | Apache-2.0 WITH LLVM-exception, legacy MIT/UIUC portions | [Apple libc++ source](https://github.com/apple-oss-distributions/libcxx), [upstream libc++](https://github.com/llvm/llvm-project/tree/llvmorg-23.1.5/libcxx); OS dylibs dynamically loaded, no dylibs redistributed |
| Windows: Microsoft C++ Standard Library, including Ryu-derived conversion code | Apache-2.0 WITH LLVM-exception; Boost-1.0 for Ryu portions | [Microsoft STL](https://github.com/microsoft/STL), [license](https://github.com/microsoft/STL/blob/main/LICENSE.txt); upstream notices below |
| Windows STL: fmt-derived formatting code by Victor Zverovich | MIT with the upstream optional compiled-object exception | [STL sources and NOTICE](https://github.com/microsoft/STL/blob/main/NOTICE.txt), notice below |
| Windows STL: William E. Kempf support code | Permissive attribution license | [STL sources and NOTICE](https://github.com/microsoft/STL/blob/main/NOTICE.txt), notice below |
| Windows STL: Unicode conversion data | Unicode permission notices (1991–1994, 1994–2011, 1991–2022) | [STL sources and NOTICE](https://github.com/microsoft/STL/blob/main/NOTICE.txt), full terms below |
| Windows: MSVC vcruntime and Universal CRT | Microsoft Visual Studio / Windows SDK redistributable terms | [Visual Studio license](https://visualstudio.microsoft.com/license-terms/), [CRT redistribution](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files); supplied by the MSVC toolchain, statically linked with `/MT`; source supplied with the toolchain in `<VCToolsInstallDir>/crt/src/vcruntime` and `<WindowsSdkDir>/Source/<version>/ucrt` |

The table conservatively includes LLVM Support implementations even where the
linker's archive selection or dead stripping omits their object code. LLVM's
upstream adaptations are used without additional modifications by cpp-distill.
No zlib, zstd, libxml2, libedit, terminfo, libpfm, curl, Z3 or HTTP library is
linked by the pinned SDK configuration: the optional dependencies are disabled
in `ci/llvm/build-sdk.py`. Release preparation checks the SDK's checksum-verified
settings and actual Ninja commands for compression/XML libraries; dynamic
binary audits enforce the platform OS-library allow-lists. The compiler/link
commands are preserved as CI evidence. Changing that dependency set requires
updating these notices and the SDK revision.

GCC and system/toolchain runtime components retain their own licenses and
exceptions; they do not change cpp-distill's GPL-3.0-or-later license. The tag's
source archive (with build scripts) plus the SDK release's llvm-project source
archive supply corresponding source for the distributed cpp-distill/LLVM work.
GCC runtime distribution uses the Runtime Library Exception; OS/toolchain
System Libraries are excluded under GPLv3's corresponding-source definition.
The user's GCC installation, its system headers and any judge libraries are
external requirements and are not bundled in the archives.

## xxHash


```text
xxHash - Extremely Fast Hash algorithm
 Copyright (C) 2012-2023, Yann Collet

 BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:

 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above
 copyright notice, this list of conditions and the following disclaimer
 in the documentation and/or other materials provided with the
 distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 You can contact the author at :
 - xxHash homepage: http://www.xxhash.com
 - xxHash source repository : https://github.com/Cyan4973/xxHash
```

## Unicode UTF conversion

```text
Copyright © 1991-2015 Unicode, Inc. All rights reserved.
Distributed under the Terms of Use in
http://www.unicode.org/copyright.html.

Permission is hereby granted, free of charge, to any person obtaining
a copy of the Unicode data files and any associated documentation
(the "Data Files") or Unicode software and any associated documentation
(the "Software") to deal in the Data Files or Software
without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, and/or sell copies of
the Data Files or Software, and to permit persons to whom the Data Files
or Software are furnished to do so, provided that
(a) this copyright and permission notice appear with all copies
of the Data Files or Software,
(b) this copyright and permission notice appear in associated
documentation, and
(c) there is clear notice in each modified Data File or in the Software
as well as in the documentation associated with the Data File(s) or
Software that the data or software has been modified.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT OF THIRD PARTY RIGHTS.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THE DATA FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder
shall not be used in advertising or otherwise to promote the sale,
use or other dealings in these Data Files or Software without prior
written authorization of the copyright holder.
```

## Regular expressions

```text
$OpenBSD: COPYRIGHT,v 1.3 2003/06/02 20:18:36 millert Exp $

Copyright 1992, 1993, 1994 Henry Spencer.  All rights reserved.
This software is not subject to any license of the American Telephone
and Telegraph Company or of the Regents of the University of California.

Permission is granted to anyone to use this software for any purpose on
any computer system, and to alter it and redistribute it, subject
to the following restrictions:

1. The author is not responsible for the consequences of use of this
   software, no matter how awful, even if they arise from flaws in it.

2. The origin of this software must not be misrepresented, either by
   explicit claim or by omission.  Since few users ever read sources,
   credits must appear in the documentation.

3. Altered versions must be plainly marked as such, and must not be
   misrepresented as being the original software.  Since few users
   ever read sources, credits must appear in the documentation.

4. This notice may not be removed or altered.

=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
/*-
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)COPYRIGHT	8.1 (Berkeley) 3/16/94
 */
```

## MD5

```text
This code is derived from (original license follows):

This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
MD5 Message-Digest Algorithm (RFC 1321).

Homepage:
http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5

Author:
Alexander Peslyak, better known as Solar Designer <solar at openwall.com>

This software was written by Alexander Peslyak in 2001.  No copyright is
claimed, and the software is hereby placed in the public domain.
In case this attempt to disclaim copyright and place the software in the
public domain is deemed null and void, then the software is
Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
general public under the following terms:

Redistribution and use in source and binary forms, with or without
modification, are permitted.

There's ABSOLUTELY NO WARRANTY, express or implied.

(This is a heavily cut-down "BSD license".)

This differs from Colin Plumb's older public domain implementation in that
no exactly 32-bit integer data type is required (any 32-bit or wider
unsigned integer data type will do), there's no compile-time endianness
configuration, and the function prototypes match OpenSSL's.  No code from
Colin Plumb's implementation has been reused; this comment merely compares
the properties of the two independent implementations.

The primary goals of this implementation are portability and ease of use.
It is meant to be fast, but not as fast as possible.  Some known
optimizations are not included to reduce source code size and avoid
compile-time configuration.
```

## GCC Runtime Library Exception

The underlying GPLv3 text is in `LICENSE`. Exception text from GCC
`releases/gcc-15.2.0/COPYING.RUNTIME`:

```text
GCC RUNTIME LIBRARY EXCEPTION

Version 3.1, 31 March 2009

Copyright (C) 2009 Free Software Foundation, Inc. <http://fsf.org/>

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.

This GCC Runtime Library Exception ("Exception") is an additional
permission under section 7 of the GNU General Public License, version
3 ("GPLv3"). It applies to a given file (the "Runtime Library") that
bears a notice placed by the copyright holder of the file stating that
the file is governed by GPLv3 along with this Exception.

When you use GCC to compile a program, GCC may combine portions of
certain GCC header files and runtime libraries with the compiled
program. The purpose of this Exception is to allow compilation of
non-GPL (including proprietary) programs to use, in this way, the
header files and runtime libraries covered by this Exception.

0. Definitions.

A file is an "Independent Module" if it either requires the Runtime
Library for execution after a Compilation Process, or makes use of an
interface provided by the Runtime Library, but is not otherwise based
on the Runtime Library.

"GCC" means a version of the GNU Compiler Collection, with or without
modifications, governed by version 3 (or a specified later version) of
the GNU General Public License (GPL) with the option of using any
subsequent versions published by the FSF.

"GPL-compatible Software" is software whose conditions of propagation,
modification and use would permit combination with GCC in accord with
the license of GCC.

"Target Code" refers to output from any compiler for a real or virtual
target processor architecture, in executable form or suitable for
input to an assembler, loader, linker and/or execution
phase. Notwithstanding that, Target Code does not include data in any
format that is used as a compiler intermediate representation, or used
for producing a compiler intermediate representation.

The "Compilation Process" transforms code entirely represented in
non-intermediate languages designed for human-written code, and/or in
Java Virtual Machine byte code, into Target Code. Thus, for example,
use of source code generators and preprocessors need not be considered
part of the Compilation Process, since the Compilation Process can be
understood as starting with the output of the generators or
preprocessors.

A Compilation Process is "Eligible" if it is done using GCC, alone or
with other GPL-compatible software, or if it is done without using any
work based on GCC. For example, using non-GPL-compatible Software to
optimize any GCC intermediate representations would not qualify as an
Eligible Compilation Process.

1. Grant of Additional Permission.

You have permission to propagate a work of Target Code formed by
combining the Runtime Library with Independent Modules, even if such
propagation would otherwise violate the terms of GPLv3, provided that
all Target Code was generated by Eligible Compilation Processes. You
may then convey such a combination under terms of your choice,
consistent with the licensing of the Independent Modules.

2. No Weakening of GCC Copyleft.

The availability of this Exception does not imply any general
presumption that third-party software is unaffected by the copyleft
requirements of the license of GCC.

```

## Microsoft C++ Standard Library notices

Source: [Microsoft STL NOTICE.txt](https://github.com/microsoft/STL/blob/main/NOTICE.txt).
The complete Apache-2.0 and LLVM exception text is in `LLVM-LICENSE.txt`.
The toolchain version is recorded in `BUILDINFO.txt`.

```text
Microsoft C++ Standard Library

Copyright (c) Microsoft Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


---- LLVM Exceptions to the Apache 2.0 License ----

As an exception, if, as a result of your compiling your source code, portions
of this Software are embedded into an Object form of such source code, you
may redistribute such embedded portions in such Object form without complying
with the conditions of Sections 4(a), 4(b) and 4(d) of the License.

In addition, if you combine or link compiled forms of this Software with
software that is licensed under the GPLv2 ("Combined Software") and if a
court of competent jurisdiction determines that the patent provision (Section
3), the indemnity provision (Section 9) or other Section of the License
conflicts with the conditions of the GPLv2, you may retroactively and
prospectively choose to deem waived or otherwise exclude such Section(s) of
the License, but only in their entirety and only with respect to the Combined
Software.

----------------------------------------------------

In addition, certain files include the notices provided below.



// Copyright 2018 Ulf Adams
// Copyright (c) Microsoft Corporation. All rights reserved.

// Boost Software License - Version 1.0 - August 17th, 2003

// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:

// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

----------------------

/*
* This file is derived from software bearing the following
* restrictions:
*
* (c) Copyright William E. Kempf 2001
*
* Permission to use, copy, modify, distribute and sell this
* software and its documentation for any purpose is hereby
* granted without fee, provided that the above copyright
* notice appear in all copies and that both that copyright
* notice and this permission notice appear in supporting
* documentation. William E. Kempf makes no representations
* about the suitability of this software for any purpose.
* It is provided "as is" without express or implied warranty.
*/

----------------------

// NOTE:
//  The conversion data in this header has been derived by Dinkumware
//  from a Unicode, Inc. file with the preamble reproduced below, under
//  the grant of right described in that preamble. This header contains
//  no other material from that file.
//
// Copyright (c) 1994-2011 Unicode, Inc.  All Rights reserved.
//
// This file is provided as-is by Unicode, Inc. (The Unicode Consortium).
// No claims are made as to fitness for any particular purpose.  No
// warranties of any kind are expressed or implied.  The recipient
// agrees to determine applicability of information provided.  If this
// file has been provided on magnetic media by Unicode, Inc., the sole
// remedy for any claim will be exchange of defective media within 90
// days of receipt.
//
//  Unicode, Inc. hereby grants the right to freely use the information
//  supplied in this file in the creation of products supporting the
//  Unicode Standard, and to make copies of this file in any form for
//  internal or external distribution as long as this notice remains
//  attached.

----------------------

/// NOTE:
//  The conversion data in this header has been derived by Dinkumware
//  from a Unicode, Inc. file with the preamble reproduced below, under
//  the grant of right described in that preamble. This header contains
//  no other material from that file.
//
//  Copyright (c) 1991-1994 Unicode, Inc.  All Rights reserved.
//
//  This file is provided as-is by Unicode, Inc. (The Unicode Consortium).
//  No claims are made as to fitness for any particular purpose.  No
//  warranties of any kind are expressed or implied.  The recipient
//  agrees to determine applicability of information provided.  If this
//  file has been provided on magnetic media by Unicode, Inc., the sole
//  remedy for any claim will be exchange of defective media within 90
//  days of receipt.
//
//  Recipient is granted the right to make copies in any form for
//  internal distribution and to freely use the information supplied
//  in the creation of products supporting Unicode.

----------------------

//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

----------------------

// Copyright (c) 2012 - present, Victor Zverovich
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// --- Optional exception to the license ---
//
// As an exception, if, as a result of your compiling your source code, portions
// of this Software are embedded into a machine-executable object form of such
// source code, you may redistribute such embedded portions in such object form
// without including the above copyright and permission notices.

----------------------

// UNICODE, INC. LICENSE AGREEMENT - DATA FILES AND SOFTWARE
//
// See Terms of Use <https://www.unicode.org/copyright.html>
// for definitions of Unicode Inc.'s Data Files and Software.
//
// NOTICE TO USER: Carefully read the following legal agreement.
// BY DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING UNICODE INC.'S
// DATA FILES ("DATA FILES"), AND/OR SOFTWARE ("SOFTWARE"),
// YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
// TERMS AND CONDITIONS OF THIS AGREEMENT.
// IF YOU DO NOT AGREE, DO NOT DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE
// THE DATA FILES OR SOFTWARE.
//
// COPYRIGHT AND PERMISSION NOTICE
//
// Copyright (c) 1991-2022 Unicode, Inc. All rights reserved.
// Distributed under the Terms of Use in https://www.unicode.org/copyright.html.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of the Unicode data files and any associated documentation
// (the "Data Files") or Unicode software and any associated documentation
// (the "Software") to deal in the Data Files or Software
// without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, and/or sell copies of
// the Data Files or Software, and to permit persons to whom the Data Files
// or Software are furnished to do so, provided that either
// (a) this copyright and permission notice appear with all copies
// of the Data Files or Software, or
// (b) this copyright and permission notice appear in associated
// Documentation.
//
// THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT OF THIRD PARTY RIGHTS.
// IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
// NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
// DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
// DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
// TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THE DATA FILES OR SOFTWARE.
//
// Except as contained in this notice, the name of a copyright holder
// shall not be used in advertising or otherwise to promote the sale,
// use or other dealings in these Data Files or Software without prior
// written authorization of the copyright holder.
```

## libc++ legacy notices

From [libcxx/LICENSE.TXT](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/libcxx/LICENSE.TXT).
The Apache-2.0 and LLVM exception text is in `LLVM-LICENSE.txt`.

```text
==============================================================================
Legacy LLVM License (https://llvm.org/docs/DeveloperPolicy.html#legacy):
==============================================================================

The libc++ library is dual licensed under both the University of Illinois
"BSD-Like" license and the MIT license.  As a user of this code you may choose
to use it under either license.  As a contributor, you agree to allow your code
to be used under both.

Full text of the relevant licenses is included below.

==============================================================================

University of Illinois/NCSA
Open Source License

Copyright (c) 2009-2019 by the contributors listed in CREDITS.TXT

All rights reserved.

Developed by:

    LLVM Team

    University of Illinois at Urbana-Champaign

    http://llvm.org

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal with
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimers.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimers in the
      documentation and/or other materials provided with the distribution.

    * Neither the names of the LLVM Team, University of Illinois at
      Urbana-Champaign, nor the names of its contributors may be used to
      endorse or promote products derived from this Software without specific
      prior written permission.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
SOFTWARE.

==============================================================================

Copyright (c) 2009-2014 by the contributors listed in CREDITS.TXT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
