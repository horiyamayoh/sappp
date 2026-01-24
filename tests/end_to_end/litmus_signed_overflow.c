// Litmus test: Signed overflow
// Expected: UNKNOWN (UB.SignedOverflow)

int main()
{
    int a = 1;
    int b = 2;
    int c = a + b;
    return c;
}
