
## 2024-05-18 - Compiler limitation hoisting `strlen()` in loops
**Learning:** The C/C++ compiler often struggles to hoist `strlen()` out of loop conditions (e.g., `for (i = 0; i <= strlen(s); i++)`) due to potential pointer aliasing or other safety constraints, even when the string is `const char*`. This leads to hidden O(N^2) complexity where the string length is recalculated on every iteration.
**Action:** Always manually cache string lengths (`strlen(s)`) in local variables before loop conditions (e.g., `size_t len = strlen(s); for (int i = 0; i <= len; ++i)`) to ensure O(N) performance, especially in code that iterates character-by-character over large strings.
