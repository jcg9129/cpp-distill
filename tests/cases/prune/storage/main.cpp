// SPDX-License-Identifier: GPL-3.0-or-later
int helper() { return 1; }
void automatic() { int local = helper(); (void)local; }
void static_local() { static int local = helper(); }
void thread_local_storage() { thread_local int local = helper(); }
int inert = 1 + 2;
int active = helper();
int main() { return 0; }
