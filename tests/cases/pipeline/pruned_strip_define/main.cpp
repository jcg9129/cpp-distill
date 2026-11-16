// SPDX-License-Identifier: GPL-3.0-or-later
#define VALUE 1 /* first
second
third */ + 2
int unused(){return 0;}
int main(){return VALUE + __LINE__;}
