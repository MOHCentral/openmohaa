## 2024-10-24 - Compiler missing strlen loop hoisting

**Learning:** Compilers often fail to hoist `strlen()` out of loop conditions, even for `const char*` or pure functions, due to potential aliasing or if there is no explicit guarantee of purity in the current translation unit. In loops checking for character matches (like `for (i = 0; i < strlen(set); i++)`), this results in O(N^2) behavior where `strlen` is evaluated every iteration. Replacing this with `strchr(set, c)` gives O(1) character lookup (or caching the length beforehand gives O(N)).
**Action:** When performing character lookups in strings within the codebase, explicitly use `strchr` instead of loop-with-`strlen`, making sure to explicitly handle `c == '\0'` since `strchr` will return a match on the null-terminator.
