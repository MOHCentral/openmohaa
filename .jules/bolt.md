## 2024-05-24 - O(N^2) strlen in for-loop conditions
**Learning:** Using `strlen(s)` inside the condition of a `for` loop (e.g. `for (i = 0; i < strlen(s); i++)`) evaluates the string length on every iteration, leading to O(N^2) complexity. This is a common pattern in older codebases.
**Action:** Replace these loops with O(N) operations by caching the string length in a variable (e.g., `size_t len = strlen(s);`) or using optimized standard library functions like `strchr(s, c)`.
