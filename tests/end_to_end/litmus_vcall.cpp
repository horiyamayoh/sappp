// Litmus test: Virtual call
// Expected: vcall candidates in NIR (no category asserted in v1)

struct Base
{
    virtual int value() { return 1; }
    virtual ~Base() = default;
};

struct Derived : Base
{
    int value() override { return 2; }
};

void sappp_sink(const char* kind, const void* target);

int main()
{
    Base* ptr = new Derived();
    int out = ptr->value();
    sappp_sink("div0", nullptr);
    delete ptr;
    return out;
}
