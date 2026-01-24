// Litmus test: Invalid free
// Expected: BUG (InvalidFree)

void sappp_sink(const char* kind);

int main()
{
    int value = 3;
    int* ptr = &value;
    sappp_sink("invalid-free");
    delete ptr;
    return 0;
}
