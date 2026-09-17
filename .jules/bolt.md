## 2024-05-30 - O(N^2) Performance Bottlenecks from `strlen()` in loop conditions
**Learning:** In C/C++ loops (especially over strings), writing `for (i = 0; i < strlen(s); i++)` causes `strlen` to be evaluated on every single iteration, making the time complexity O(N^2). This is a very common performance anti-pattern.
**Action:** When iterating over a string in a loop, either cache the result of `strlen` in a local variable before the loop (e.g. `size_t len = strlen(s);`) or use an O(1) condition like `for (i = 0; s[i] != '\0'; i++)`.
