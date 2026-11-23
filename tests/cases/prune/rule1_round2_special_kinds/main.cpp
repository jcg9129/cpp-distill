// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Default {
    Default() = delete;
    Default(long) {}
    Default(unsigned long) {}
};
struct HasDefault { Default value; };
struct Copy {
    Copy() = default; Copy(const Copy&) = delete; Copy(Copy&&) = default;
    Copy(long) {}
    Copy(unsigned long) {}
};
struct HasCopy { Copy value; };
struct Move {
    Move() = default; Move(const Move&) = default; Move(Move&&) = delete;
    Move(long) {}
    Move(unsigned long) {}
};
struct HasMove { Move value; };
struct Assign {
    Assign() = default; Assign& operator=(const Assign&) = delete; Assign& operator=(Assign&&) = default;
    Assign(long) {}
    Assign(unsigned long) {}
};
struct HasAssign { Assign value; };
struct Destructor {
    Destructor() = default; ~Destructor() = delete;
    Destructor(long) {}
    Destructor(unsigned long) {}
};
struct HasDestructor { Destructor value; };
int main() {
    int result = (sizeof(HasDefault) + sizeof(HasCopy) + sizeof(HasMove) + sizeof(HasAssign) + sizeof(HasDestructor)) == 0;
    std::printf("%d\n", result);
    return result;
}
