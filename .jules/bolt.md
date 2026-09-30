## 2025-02-14 - Optimize Com_HexStrToInt Loop Condition
**Learning:** Checking string length with `strlen()` inside the condition of a `for` loop (e.g., `for (i = 0; i < strlen(str); i++)`) recalculates the length on every iteration, turning an O(N) operation into an O(N^2) bottleneck.
**Action:** Replace `strlen(str)` inside loop conditions with a direct array null-terminator check (e.g., `str[i] != '\0'`) to eliminate the overhead, making the string parsing inherently O(N).
