# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure an explicitly elementary API option for the precision-five example.

This is an analysis prototype, not a replacement for ATL's fast composition API.
The prototype keeps the artifact's entire mod_int dependency surface, so its
size does not depend on speculative downstream pruning of that surface.
"""
from common import *
from candidates import verify_one
import os
import subprocess

KERNEL = r'''
namespace atl {
template<typename Coefficient>
std::vector<Coefficient> elementary_product(std::vector<Coefficient> const &left,
                                         std::vector<Coefficient> const &right, std::size_t length) {
    std::vector<Coefficient> result(length);
    for (std::size_t i = 0; i < left.size() && i < length; ++i)
        for (std::size_t j = 0; j < right.size() && j < length - i; ++j)
            result[i + j] += left[i] * right[j];
    return result;
}
template<typename Coefficient>
std::vector<Coefficient> elementary_composition(std::vector<Coefficient> const &outer,
                                             std::vector<Coefficient> const &inner, std::size_t length) {
    std::vector<Coefficient> result(length);
    if (length == 0) return result;
    for (std::size_t i = outer.size(); i > 0; --i) {
        result = elementary_product(result, inner, length);
        result[0] += outer[i - 1];
    }
    return result;
}
template<typename Coefficient>
std::vector<Coefficient> elementary_reversion(std::vector<Coefficient> series, std::size_t length) {
    series.resize(length);
    std::vector<Coefficient> result(length);
    if (length < 2) return result;
    auto const scale = Coefficient(1) / series[1];
    std::vector<std::vector<Coefficient>> powers(length, std::vector<Coefficient>(length));
    powers[0][0] = Coefficient(1);
    for (std::size_t degree = 1; degree < length; ++degree) {
        Coefficient residual(0);
        for (std::size_t exponent = 2; exponent <= degree; ++exponent) {
            for (std::size_t i = 1; i < degree; ++i)
                powers[exponent][degree] += result[i] * powers[exponent - 1][degree - i];
            residual += series[exponent] * powers[exponent][degree];
        }
        result[degree] = (Coefficient(degree == 1 ? 1 : 0) - residual) * scale;
        powers[1][degree] = result[degree];
    }
    return result;
}
template<typename Coefficient>
std::vector<Coefficient> elementary_projection(std::vector<Coefficient> const &weights,
                                            std::vector<Coefficient> series, std::size_t count) {
    series.resize(weights.size());
    std::vector<Coefficient> result(count), power(weights.size());
    if (weights.empty()) return result;
    power[0] = Coefficient(1);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = 0; j < weights.size(); ++j) result[i] += weights[j] * power[j];
        if (i + 1 < count) power = elementary_product(power, series, weights.size());
    }
    return result;
}
}
'''

CHECK = r'''
int main() {
    using coefficient = atl::mod_int32<998244353>;
    unsigned state = 123456789;
    auto sample = [&] { state = state * 1664525u + 1013904223u; return coefficient(state % 17); };
    for (std::size_t length = 0; length <= 12; ++length) {
        for (int trial = 0; trial < 20; ++trial) {
            std::vector<coefficient> outer(1 + trial % 7), inner(length), weights(length);
            for (auto &v : outer) v = sample();
            for (auto &v : inner) v = sample();
            for (auto &v : weights) v = sample();
            if (atl::elementary_composition(outer, inner, length) != atl::series_composition(outer, inner, length)) return 1;
            if (atl::elementary_projection(weights, inner, length) != atl::series_power_projection(weights, inner, length)) return 2;
            if (length) inner[0] = coefficient(0);
            if (length > 1) inner[1] = coefficient(1 + trial % 16);
            auto const inverse = atl::elementary_reversion(inner, length);
            if (inverse != atl::series_compositional_inverse(inner, length)) return 3;
            auto identity = std::vector<coefficient>(length);
            if (length > 1) identity[1] = coefficient(1);
            if (atl::elementary_composition(inner, inverse, length) != identity) return 4;
        }
    }
}
'''


def main():
    example = 'series-composition-quick'
    x = next(x for x in json.loads((OUT/'analysis.json').read_text()) if x['example']==example)
    needed = set()
    def visit(header):
        if header in needed or header == 'atl/types/int128.hpp':
            return
        needed.add(header)
        for include in re.findall(r'(?m)^\s*#include\s*[<"](atl/[^>"]+)[>"]', (ATL/'include'/header).read_text()):
            visit(include)
    visit('atl/nt/mod_int.hpp')
    buf = (folder(example)/'distill-compact.cpp').read_bytes()
    core = b''.join(line for line,origin in zip(buf.splitlines(keepends=True),x['line_map']) if origin[0] in needed)
    main_decl = next(d for d in x['declarations'] if d['name']=='main')
    main_text = buf[main_decl['start']:main_decl['end']]
    for old,new in [(b'series_composition',b'elementary_composition'),
                    (b'series_compositional_inverse',b'elementary_reversion'),
                    (b'series_power_projection',b'elementary_projection')]:
        main_text = main_text.replace(b'atl::'+old+b'(',b'atl::'+new+b'(')
    compact_kernel = '\n'.join(line.strip() for line in KERNEL.splitlines() if line.strip()).encode()+b'\n'
    code = core + b'#include <cstddef>\n#include <iostream>\n#include <vector>\n' + compact_kernel + main_text+b'\n'
    result = verify_one((example,'E',code))
    result.update(saved=len(buf)-len(code),projected=(len(code)*8736+9999)//10000,kernel_bytes=len(compact_kernel),core_bytes=len(core))
    dest = OUT/'prototypes/E'/example
    check_source = dest/'equivalence.cpp'
    check_source.write_text('#include <atl/nt/series_composition.hpp>\n'+KERNEL+CHECK)
    compile_result = subprocess.run(['g++','-std=c++23','-O0','-I'+str(ATL/'include'),str(check_source),'-o',str(dest/'equivalence')],
                                    capture_output=True,timeout=180,env=dict(os.environ,TMPDIR=str(OUT/'tmp')))
    (dest/'equivalence.log').write_bytes(compile_result.stdout+compile_result.stderr)
    assert compile_result.returncode==0,compile_result.stderr.decode()[-3000:]
    run = subprocess.run([str(dest/'equivalence')],cwd=dest,capture_output=True,timeout=30)
    assert run.returncode==0,(run.returncode,run.stderr)
    result['equivalence_cases']=260
    dump('elementary.json',result)
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
