// Litmus test: Use-after-lifetime
// Expected: UNKNOWN (UseAfterLifetime)

void sappp_sink(const char* kind);

int main()
{
    int* ptr = nullptr;
    {
        int use_after_lifetime = 42;
        ptr = &use_after_lifetime;
    }
    sappp_sink("use-after-lifetime");
    return *ptr;
}
