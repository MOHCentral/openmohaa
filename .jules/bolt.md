## 2024-05-18 - [Optimize `Field_FindFirstSeparator` string search]
**Learning:** Calling `strlen()` inside the condition of a `for` loop over a string forces the length to be re-evaluated on every iteration if the compiler cannot optimize it out. This creates a severe `O(N^2)` bottleneck for long strings in this codebase.
**Action:** Always replace `for (i = 0; i < strlen(s); i++)` style loops with standard C library functions like `strchr` when searching for characters, or cache the `strlen` result in a local variable before the loop.
