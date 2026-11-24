#include <cstddef>
#include \
 <cstddef> /* multi
comment */
constexpr auto raw = R"tag(
#include <cstddef>
)tag";
static_assert(__LINE__ == 8);
int main() { return raw[0] != '\n'; }
