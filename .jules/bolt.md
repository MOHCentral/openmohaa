
## 2024-05-18 - Replacing O(n^2) loop calling strlen with strchr
**Learning:** Replacing an O(n^2) loop like `for( i = 0; i < strlen( set ); i++ )` containing an equality check against characters in `set` with `strchr(set, c)` requires an explicit check for the null terminator. `strchr` successfully matches the null terminator (`\0`), but the original `strlen`-based loop condition excluded it, causing semantic differences and potential infinite loops.
**Action:** When replacing a `strlen` character matching loop with `strchr` for O(1) length lookup, explicitly verify if `c == '\0'` first (e.g., `if (c == '\0') return qfalse;`).
