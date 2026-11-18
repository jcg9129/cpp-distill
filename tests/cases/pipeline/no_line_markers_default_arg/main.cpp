// SPDX-License-Identifier: GPL-3.0-or-later
int unused(){return 2;}
int where(int line = __builtin_LINE()){return line;}
int main(){return where() != 4;}
