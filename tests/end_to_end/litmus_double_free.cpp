// Litmus test: Double free
// Expected: BUG (DoubleFree)

void sappp_sink(const char* kind, const void* target);

struct Widget
{
    int value = 0;
    ~Widget() {}
};

int main()
{
    Widget* ptr = new Widget{7};
    delete ptr;
    sappp_sink("double-free", ptr);
    delete ptr;
    return 0;
}
