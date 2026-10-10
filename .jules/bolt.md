## 2024-05-13 - O(N^2) strlen bottleneck in loops
**Learning:** Found an O(N^2) performance pattern where `strlen()` is called inside loop conditions (e.g. `for (i = 0; i < strlen(s); i++)`). The compiler cannot hoist `strlen` out of the loop condition.
**Action:** Replace these manual search loops with O(N) standard library functions like `strchr` when searching for characters, or cache the `strlen` result in a local variable before the loop.
