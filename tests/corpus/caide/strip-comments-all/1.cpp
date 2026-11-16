// leading line comment
#include <string>

/* block comment
   spanning lines */
const char* url() {
    return "scheme://not-a-comment"; // trailing comment
}

int main() {
    return url()[0]; /* inline */
}
