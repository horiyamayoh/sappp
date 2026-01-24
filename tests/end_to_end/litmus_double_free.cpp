// Litmus test: Double free
// Expected: BUG (DoubleFree)

void sappp_sink(const char* kind);

int main()
{
    int* ptr = new int(7);
    delete ptr;
    sappp_sink("double-free");
    delete ptr;
    return 0;
}
