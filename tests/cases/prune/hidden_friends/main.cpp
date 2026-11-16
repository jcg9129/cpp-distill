// SPDX-License-Identifier: GPL-3.0-or-later
struct Box { friend void iter_move(Box) {} };
struct Other { friend void unused(Other) {} };
int main() { return 0; }
