
## 2024-05-18 - C String Length Bottleneck inside loop condition
**Learning:** Using `strlen()` inside a `for` loop condition (e.g. `for (i = 0; i < strlen(info); i++)`) can create a severe O(N^2) bottleneck, especially for large strings, because the length is recalculated on every iteration.
**Action:** When iterating over C strings, always cache the result of `strlen()` before the loop if the string's length doesn't change during iteration, or use direct null-terminator checks `info[i] != '\0'`.
