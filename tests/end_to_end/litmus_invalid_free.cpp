// Litmus test: Invalid free
// Expected: BUG (InvalidFree)

void sappp_sink(const char* kind, const void* target);

int main()
{
    int value = 7;
    int* ptr = &value;
    sappp_sink("invalid-free", ptr);
    delete ptr;
    return 0;
}
