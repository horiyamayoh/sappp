// Litmus test: Division by zero
// Expected: UNKNOWN (div0 reachability not proven in v1)

int divide(int a, int b)
{
    return a / b;  // PO: b != 0
}

int main(void)
{
    int x = 10;
    int y = 0;
    return divide(x, y);  // BUG: y == 0
}
