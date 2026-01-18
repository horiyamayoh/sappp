// Litmus test: Division by zero
// Expected: BUG (div0 is reachable)

int divide(int a, int b)
{
    return a / b;  // PO: b != 0
}

int main()
{
    int x = 10;
    int y = 0;
    return divide(x, y);  // BUG: y == 0
}
