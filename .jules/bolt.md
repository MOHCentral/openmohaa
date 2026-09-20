## 2024-05-24 - O(N^2) loop bottleneck due to strlen in C tool strings
**Learning:** O(N^2) bottlenecks in tools like `code/tools/asm/cmdlib.c` and `code/tools/ommap/common/cmdlib.c` can be caused by using `strlen()` inside `for` loop conditions.
**Action:** Optimize these to O(N) by checking directly for the null-terminator in the array (e.g., `qdir[i]`) instead of evaluating `strlen()` on every iteration.
