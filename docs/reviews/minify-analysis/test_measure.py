"""Focused tests for boundaries whose corruption would invalidate the estimate."""
import unittest
from measure import (lex, render, signature, exclusions, mapping_for, cleanup,
                     macro_exclusions, KEYWORDS)


class MinifyTests(unittest.TestCase):
    def check(self, source):
        tokens = lex(source)
        output = render(source, tokens, minimal=True)
        self.assertEqual(signature(tokens), signature(lex(output)))
        self.assertEqual(output, render(output, lex(output), minimal=True))
        return output

    def test_word_number_operator_boundaries(self):
        self.check('int value = 1; value + + value; value / *pointer; '
                   'auto z = 1e2 + 3; auto w = 1 . x; int p = 0x1f;')

    def test_digraph_and_ellipsis(self):
        self.check('x < ::T > y; x < :: > y; . . . ; a % : b; a < : b;')

    def test_literals_exact(self):
        source = r'''auto s = u8"foo \" quote"; auto c = '\n'; ''' + 'auto r=R"tag(a\n b\\\n c)tag";'
        output = self.check(source)
        self.assertEqual(sum(t.kind=='literal' for t in lex(source)),3)
        self.assertEqual([t.text for t in lex(source) if t.kind == 'literal'],
                         [t.text for t in lex(output) if t.kind == 'literal'])

    def test_comments(self):
        self.check('int x; // keep this line\n int y; /* keep\n these */ int z;')
        self.check('x / /* keep */ y; x / // keep\n y;')

    def test_literal_prefix_boundary(self):
        self.check('#define X R "text"\n')

    def test_directives_and_function_macros(self):
        source = '#define OBJ (1)\n#define FUN(x) \\\n (x + 1)\nint main() { return FUN(OBJ); }\n'
        output = self.check(source)
        self.assertIn('#define OBJ (1)', output)
        self.assertIn('#define FUN(x)', output)
        self.assertIn('\\\n', output)
        self.assertIn('\nint main', output)

    def test_include_header_names(self):
        output = self.check('#include <foo/bar.h>\n#include "a//b.h"\nint i;')
        self.assertIn('#include<foo/bar.h>',output)

    def test_macro_protection(self):
        source = '#define S(x) #x helper\n#define P(a,b) a ## b extra\n#define F(y) y+normal\n'
        kept, _, _ = macro_exclusions(lex(source), source)
        self.assertTrue({'S','x','helper','P','a','b','extra','F','y'} <= kept)
        self.assertNotIn('normal', kept)

    def test_exclusions(self):
        source = '[[custom::tag]] int named; const char*s="named other"; int __x; int _X;'
        kept, _, _ = exclusions(lex(source), source, {'library'}, {'swap'})
        self.assertTrue({'custom','tag','named','other','__x','_X','library','swap','main'} <= kept)

    def test_string_contents_not_char_or_raw_delimiter(self):
        source = 'R"delimiter(contents)delimiter"; "f\\157o"; \'x\';'
        kept, _, _ = exclusions(lex(source),source,set(),set())
        self.assertTrue({'contents','foo'} <= kept)
        self.assertFalse({'delimiter','R','x'} & kept)

    def test_simultaneous_shortening(self):
        source = 'long_name long_name a a a short_name;'
        mapping, counts = mapping_for(lex(source), {'long_name','a','short_name'}, set(KEYWORDS))
        self.assertEqual(mapping['a'], 'a')
        self.assertEqual(mapping['long_name'], 'b')
        self.assertEqual(mapping['short_name'], 'c')
        output = render(source, lex(source), mapping)
        self.assertEqual(output, 'b b a a a c;')

    def test_cleanup_counts_original_lines(self):
        source = '#include <vector>\n#include <vector>\n#define DISTILL_ONCE_x\n#define DISTILL_ONCE_y\nint DISTILL_ONCE_y;'
        clean, inc, once = cleanup(source, lex(source))
        self.assertEqual(inc, len('#include <vector>\n'))
        self.assertEqual(once, len('#define DISTILL_ONCE_x\n'))
        self.assertIn('#define DISTILL_ONCE_y', clean)
        self.assertEqual(clean.count('#include'), 1)


if __name__ == '__main__':
    unittest.main()
