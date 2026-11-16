// SPDX-License-Identifier: GPL-3.0-or-later
int ghost(){return 9;}
int main(){ // ghost is mentioned only in trivia and a string.
 const char* s="ghost"; return s[0]=='g' ? 0 : 1;
}
