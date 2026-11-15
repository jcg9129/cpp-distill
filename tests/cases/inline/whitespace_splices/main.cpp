// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" int printf(const char*, ...);
// space \  
#include "missing-space.h"
// tab \	
#include "missing-tab.h"
// form feed \
#include "missing-form-feed.h"
// vertical tab \
#include "missing-vertical-tab.h"
// CRLF \ 	
#include "missing-crlf.h"
/* block *\ 	
/ #inc\
lude \
"a.h" // ignored include tail \ 	
#include "missing-tail.h"
const char* ordinary = "a\ 	
b";
const char* raw = R"tag(one\  
two\	
three\
four\
five\ 	
six
)tag\ 	
"
#include "missing-raw.h"
)tag";
#define RAW R"(macro\
body)" " suffix"
int main() {
  printf("%s %s %s %d %s\n", ordinary, raw, RAW, __LINE__, __FILE__);
  return value != 7 || ordinary[1] != 'b' || raw[4] != ' ';
}
