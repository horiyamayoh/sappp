// Litmus test: Null pointer dereference
// Expected: BUG (null deref is reachable)

int deref(const int* ptr)
{
    return *ptr;  // PO: ptr != nullptr
}

int main()
{
    int* p = nullptr;
    return deref(p);  // BUG: p == nullptr
}
