
## 2024-05-18 - Caching strlen in loop condition
**Learning:** Found an O(N^2) bottleneck in `Script::EvaluateMacroString` where `strlen(theMacroString)` was evaluated on every iteration of a string parsing loop. The compiler couldn't optimize it out because `strlen` is not guaranteed to be pure (though it effectively is on const strings without aliasing, compilers can be conservative). For 250 character strings, caching `strlen` in a local variable `len` resulted in a ~3.6x speedup.
**Action:** Always manually cache the result of `strlen()` before `for` or `while` loops iterating over C-strings to ensure O(N) performance, rather than relying on the compiler to optimize the loop condition.
