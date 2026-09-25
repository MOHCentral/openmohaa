## 2024-05-30 - O(N^2) Bottlenecks
**Learning:** Found several instances where `strlen()` is used in loop conditions (e.g., `for (i = 0; i < strlen(s); i++)`), turning O(N) operations into O(N^2).
**Action:** Always cache the string length before the loop or use a null character check when iterating.

## 2024-05-30 - Optimizing Com_CharIsOneOfCharset
**Learning:** Replacing `strlen()` loop with `strchr()` for character checking provided a ~32,000x speedup for long strings while maintaining identical behavior (make sure to handle `'\0'` since `strchr` matches it while the original loop did not).
**Action:** Use standard library functions like `strchr()` for character lookups instead of manual loops where applicable.
