// Litmus test: Use-after-lifetime
// Expected: BUG (UseAfterLifetime)

void sappp_sink(const char* kind, const void* target);

struct Widget
{
    int value = 0;
    ~Widget() {}
};

int main()
{
    Widget* ptr = nullptr;
    {
        Widget use_after_lifetime{42};
        ptr = &use_after_lifetime;
    }
    sappp_sink("use-after-lifetime", "use_after_lifetime");
    return ptr->value;
}
