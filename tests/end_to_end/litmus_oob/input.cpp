// Litmus test: Out-of-bounds array access
// Expected: BUG (OOB is reachable)

int access(int* arr, int len, int idx)
{
    return arr[idx];  // PO: 0 <= idx < len
}

int main()
{
    int arr[5] = {1, 2, 3, 4, 5};
    return access(arr, 5, 10);  // BUG: idx >= len
}
