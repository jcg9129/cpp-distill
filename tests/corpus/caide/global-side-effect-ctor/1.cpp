// A global whose constructor has a side effect is never referenced by name,
// but removing it changes the program's result (main returns 7, not 0).
int state = 0;

struct Boot {
    Boot() { state = 7; }
};

Boot boot;

int main() {
    return state;
}
