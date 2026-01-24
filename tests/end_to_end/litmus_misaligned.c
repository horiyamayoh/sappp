// Litmus test: Misaligned access
// Expected: UNKNOWN (UB.Misaligned)

int main()
{
    char buffer[sizeof(int) + 1];
    int* ptr = (int*)(buffer + 1);
    return ptr != 0;
}
