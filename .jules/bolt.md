## 2024-11-20 - [Avoid strlen in loops]
**Learning:** In the Quake III Arena/MoHAA codebase, calling `strlen(s)` in a `for` loop condition evaluates it on every iteration, leading to O(N^2) complexity. This is especially bad for long input strings.
**Action:** Replace `strlen(s)` with checking `s[i] != '\0'` in the condition, or if the code looks for a single character, use standard C library functions like `strchr` instead.
