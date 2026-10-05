## 2024-06-25 - C String Loop Performance Optimizations
**Learning:** Found an O(N^2) performance anti-pattern where `strlen()` was being evaluated inside character search loop conditions in `Com_CharIsOneOfCharset` and `Field_FindFirstSeparator`. Replaced these loops with optimized `strchr()` calls, which reduced the search complexity to O(N).
**Action:** When replacing a `strlen()`-based character matching loop with `strchr()`, always ensure `c == '\0'` is handled explicitly if the original loop excluded the null terminator, as `strchr()` successfully matches it.
