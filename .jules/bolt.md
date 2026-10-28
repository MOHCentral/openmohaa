## 2024-05-24 - Avoiding semantic bugs when replacing strlen loop with strchr
**Learning:** `strchr(set, c)` successfully matches the null terminator (`\0`), while `for (i = 0; i < strlen(set); i++)` loop conditions naturally exclude it. Replacing the loop directly with `strchr` without checking if `c == '\0'` introduces semantic differences and can lead to bugs or infinite loops.
**Action:** Always explicitly check for `c == '\0'` before calling `strchr` when converting from a `strlen` bounded loop that checks characters against a set.
