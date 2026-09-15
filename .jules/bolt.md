## 2024-06-25 - C String Functions in For Loops
**Learning:** Found a classic C performance anti-pattern where an O(N) search logic inadvertently turns into O(N^2) because `strlen(s)` is called inside the `for` loop condition.
**Action:** Always replace manual loops tracking characters (especially with `strlen` in the condition) with highly optimized standard library functions like `strchr` when possible.
