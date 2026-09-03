## 2024-05-24 - [O(N^2) strlen in loop conditions]
**Learning:** Found an anti-pattern specific to older C codebases where `strlen()` is used directly inside loop conditions, causing an O(N^2) bottleneck as string length is re-evaluated on every iteration.
**Action:** Next time, search for `for.*strlen` and replace it by caching the length in a local variable before the loop, or use standard library functions like `strchr` if applicable.
