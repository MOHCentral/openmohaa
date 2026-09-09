## 2024-05-19 - O(N^2) loops with strlen
**Learning:** Found an O(N^2) bottleneck in C code: `strlen` evaluated inside a `for` loop condition `for(i=0; i<strlen(info); i++)` which can have a high execution time if the string `info` is long and called frequently, in this case inside `CL_CheckForResend`.
**Action:** Always cache the value returned from `strlen` using an external variable when iterating through a string if the length isn't mutated in the loop to reduce the string search from O(N^2) to O(N).
