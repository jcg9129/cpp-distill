// `reserved` is never named, but sizeof(Packet) depends on it: removing the
// field changes the result (main must return 0).
struct Packet {
    unsigned char reserved[32];
    int value;
};

int main() {
    Packet p;
    p.value = 1;
    return (sizeof(Packet) == 36 && p.value == 1) ? 0 : 1;
}
