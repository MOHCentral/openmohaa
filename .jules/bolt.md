## 2026-08-29 - Optimize Field_FindFirstSeparator
**Learning:** Found an O(N^2) loop where `strlen(s)` was repeatedly called in the condition of a loop meant to find the first semicolon character in a string. This can be directly replaced with `strchr(s, ';')`, which is O(N) and internally optimized.
**Action:** Replaced the loop with `strchr`, significantly boosting performance.
