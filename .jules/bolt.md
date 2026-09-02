## 2026-09-02 - [O(N^2) strlen bottleneck in loops]
**Learning:** In C codebases, using `strlen(s)` in a `for` loop condition (`for (i = 0; i < strlen(s); i++)`) is a common anti-pattern that causes an $O(N^2)$ bottleneck because `strlen` is evaluated on every iteration.
**Action:** Always replace such loops with standard library calls like `strchr(s, ';')` when searching for a character, or cache the length variable before the loop (`size_t len = strlen(s);`).
