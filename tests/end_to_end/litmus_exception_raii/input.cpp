#include <stdexcept>

struct Guard {
    explicit Guard(int* out) : out_(out) {}
    ~Guard() { if (out_) { *out_ = 42; } }

    int* out_;
};

int may_throw(bool flag, int* out) {
    Guard g(out);
    if (flag) {
        throw std::runtime_error("boom");
    }
    return 1;
}

int test(bool flag, int* out) {
    Guard g(out);
    return 10 / may_throw(flag, out);
}
