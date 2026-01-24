// Litmus test: Double free
// Expected: UNKNOWN (DoubleFree)

void sappp_sink(const char* kind);

int main()
{
    int* ptr = new int(7);
    delete ptr;
    sappp_sink("double_free");
    delete ptr;
    return 0;
}
