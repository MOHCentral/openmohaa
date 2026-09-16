## 2026-09-16 - [O(N^2) Bottlenecks from `strlen` in loop conditions]
**Learning:** Found multiple instances where `strlen()` was evaluated on every iteration of a loop (e.g., `for (i = 0; i < strlen(s); i++)`), causing O(N^2) complexity. This is particularly problematic for string parsing or info string construction in Quake-based networking code where string sizes can be substantial.
**Action:** Always cache the length in a local variable (e.g., `size_t len = strlen(s);`) before the loop, or use optimized string searching functions like `strchr(s, ';')` to improve complexity to O(N).
