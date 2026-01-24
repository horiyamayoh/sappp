// Litmus test: Exception RAII
// Expected: SAFE (UB.Shift) plus exception/lifetime ops in NIR

void sappp_check(const char* kind, bool predicate);

struct Guard
{
    ~Guard() {}
};

void may_throw();

int main()
{
    try {
        Guard guard;
        may_throw();
        sappp_check("shift", false);
        throw 1;
    } catch (...) {
        return 0;
    }
}
