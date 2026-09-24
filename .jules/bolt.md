## 2024-05-18 - [Replaced O(N^2) strlen loop with O(N) strchr in qcommon/common.c]
**Learning:** Found an O(N^2) bottleneck where `strlen()` was used inside a `for` loop condition checking for a specific character (`;`). Standard C library string functions like `strchr()` provide O(N) complexity (and often hardware-optimized assembly implementation) for these lookups.
**Action:** When searching C codebase, look for `for (i = 0; i < strlen(s); i++)` loops performing character lookups and replace them with `strchr()` or cache the `strlen()` result.
