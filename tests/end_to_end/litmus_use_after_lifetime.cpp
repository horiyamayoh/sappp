// Litmus test: Use-after-lifetime
// Expected: BUG (UseAfterLifetime)

void sappp_sink(const char* kind, const void* target);

int main()
{
    int* ptr = nullptr;
    {
        int use_after_lifetime = 42;
        ptr = &use_after_lifetime;
    }
    sappp_sink("use-after-lifetime", "use_after_lifetime");
    return *ptr;
}
