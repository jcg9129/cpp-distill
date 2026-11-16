int state = 0;

int initialize() {
    state += 2;
    return state;
}

int dynamicGlobal = initialize();
thread_local int dynamicThread = initialize();
int constantGlobal = 42;
int zeroGlobal;

struct StaticOwner {
    static int dynamicMember;
    static int constantMember;
};

int StaticOwner::dynamicMember = initialize();
int StaticOwner::constantMember = 42;

struct Finalizer {
    ~Finalizer() { state += 10; }
};

Finalizer finalizers[2] = {};
thread_local Finalizer threadFinalizer = {};

__attribute__((constructor)) void startup() { state += 1; }
__attribute__((destructor)) void shutdown() { state += 1; }
__attribute__((used)) int retainedConstant = 42;
__attribute__((used)) int retainedFunction() { return 42; }

template<class T>
int unusedVariableTemplate = initialize();

template<class T>
struct UnusedTemplate {
    static int member;
};

template<class T>
int UnusedTemplate<T>::member = initialize();

template<class T>
struct UsedTemplate {
    static int member;
};

template<class T>
int UsedTemplate<T>::member = initialize();

int unusedFunction() {
    static int local = initialize();
    return local;
}

int main() {
    UsedTemplate<int> object;
    (void)object;
    return state == 5 ? 0 : 1;
}
