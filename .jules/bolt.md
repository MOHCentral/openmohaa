## 2024-03-21 - [O(N^2) strlen loops]
**Learning:** Found an O(N^2) time complexity performance trap. In `code/qcommon/common.c`, a `for` loop used `strlen(s)` in its condition for finding a character: `for (i = 0; i < strlen(s); i++)`. Since `strlen` is O(N) and evaluates on each loop iteration, the whole loop becomes O(N^2).
**Action:** Replace manual loops that search for a character with optimized standard library functions like `strchr`, which process the string in O(N).
