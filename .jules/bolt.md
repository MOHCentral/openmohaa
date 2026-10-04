
## 2024-05-18 - Replacing strlen() loops with strchr() for O(1) character lookup
**Learning:** In C, checking if a character belongs to a charset using a loop like `for(i=0; i<strlen(set); i++) { if(set[i] == c) return qtrue; }` can be optimized by using the standard library function `strchr(set, c)`. Since `strchr` has optimized implementations, it's significantly faster than manually iterating with a loop that potentially calls `strlen` repeatedly (especially when not unrolled or optimized by the compiler).
**Action:** Replace `strlen`-based loops for character lookup with `strchr`, but explicitly handle the null terminator `if (c == '\0') return qfalse;` since `strchr` considers the null character part of the string, while the original loop excluded it.
