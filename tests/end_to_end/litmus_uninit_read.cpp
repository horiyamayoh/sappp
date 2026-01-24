// Litmus test: Uninitialized read
// Expected: BUG (UninitRead)

void sappp_sink(const char* kind, int target);

int main()
{
    int value;
    sappp_sink("uninit_read", value);
    return value;
}
