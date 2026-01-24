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

int main()
{
    Base* ptr = new Derived();
    int out = ptr->value();
    delete ptr;
    return out;
}
