// SPDX-License-Identifier: GPL-3.0-or-later
#include <system_error>
namespace app { enum class Err { bad = 1 }; }
namespace std {
template<> struct is_error_code_enum<app::Err> : true_type {};
template<> struct is_error_condition_enum<app::Err> : true_type {};
}
namespace app {
std::error_code make_error_code(Err e){return {int(e),std::generic_category()};}
std::error_condition make_error_condition(Err e){return {int(e),std::generic_category()};}
}
int unused(){return 9;}
int main(){std::error_code a = app::Err::bad; std::error_condition b = app::Err::bad; return a.value()+b.value()==2 ? 0 : 1;}
