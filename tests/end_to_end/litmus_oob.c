// Litmus test: Out-of-bounds array access
// Expected: BUG (OOB is reachable)

int access(const int* arr, int idx)
{
    return arr[idx];  // PO: 0 <= idx < len
}

int main(void)
{
    int arr[5] = {1, 2, 3, 4, 5};
    return access(arr, 10);  // BUG: idx >= len
}
