
## 2024-05-14 - O(N²) String Bottlenecks in Loop Conditions
**Learning:** Found multiple instances where `strlen()` was used directly in `for` loop conditions across the codebase. Due to potential side-effects and string aliasing, the C/C++ compilers cannot optimize away `strlen()` calls, leading to an implicit O(N²) complexity for simple O(N) operations like string copying or character checking.
**Action:** Always cache `strlen()` results into a local variable before using them in a loop condition, or rewrite the logic to use O(1) standard library lookups like `strchr()` or check directly for the null terminator `\0`.
