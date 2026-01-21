// Litmus test: Null pointer dereference
// Expected: BUG (null deref is reachable)

int deref(const int* ptr)
{
    return *ptr;  // PO: ptr != 0
}

int main(void)
{
    int* p = (int*)0;
    return deref(p);  // BUG: p == 0
}
