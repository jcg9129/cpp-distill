// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <sstream>
namespace app {
struct Date { int n=0; };
std::istream& from_stream(std::istream& in,const char*,Date& d){return in>>d.n;}
}
int main(){std::istringstream in("7"); app::Date d; in>>std::chrono::parse("%Y",d); return d.n==7 ? 0 : 1;}
