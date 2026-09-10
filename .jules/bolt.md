## 2024-05-24 - O(N^2) String processing bottleneck in loops
**Learning:** Using `strlen(s)` inside a `for` loop condition creates an O(N^2) performance bottleneck because `strlen` is evaluated on every iteration. This is a common C anti-pattern.
**Action:** Replace loop-based character searches with highly optimized standard library functions like `strchr`, or cache the result of `strlen` in a local variable before the loop.
