## 2024-05-18 - [Optimize Field_FindFirstSeparator]
**Learning:** Found a textbook O(N^2) issue in `code/qcommon/common.c` where `strlen(s)` was evaluated in a loop condition character-by-character to find a semicolon.
**Action:** Replaced loop with `strchr(s, ';')`, significantly increasing parsing performance. A good pattern to watch out for across this older C codebase.
