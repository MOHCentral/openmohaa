## 2024-05-18 - [Fix O(N^2) strlen in loop condition for Field_FindFirstSeparator]
**Learning:** In `code/qcommon/common.c`, `Field_FindFirstSeparator` was using a `for` loop with `strlen(s)` in its condition to search for a semicolon (`;`). This created an O(N^2) bottleneck. A similar issue was found earlier with `Com_CharIsOneOfCharset`.
**Action:** When replacing such loops with `strchr`, verify that it returns exactly the correct pointer or `NULL` and correctly drops the O(N^2) complexity to O(N).
