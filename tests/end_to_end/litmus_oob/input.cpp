// Litmus test: Out-of-bounds array access
// Expected: BUG (OOB is reachable)

#include <array>
#include <cstddef>
#include <span>

int access(std::span<const int> arr, std::size_t idx)
{
    return arr[idx];  // PO: 0 <= idx < len
}

int main()
{
    std::array<int, 5> arr{1, 2, 3, 4, 5};
    return access(arr, 10);  // BUG: idx >= len
}
