// Litmus test: Uninitialized read
// Expected: UNKNOWN (UninitRead)

void sappp_sink(const char* kind);

int main()
{
    int value;
    sappp_sink("uninit_read");
    return value;
}
