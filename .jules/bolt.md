## 2024-05-24 - Quake Engine `switch` Statement Scoping
**Learning:** In Quake-based codebase functions like `CL_CheckForResend` in `cl_main.cpp`, declaring and initializing a variable (e.g., `size_t info_len = strlen(info)`) inside a `switch` statement can cause a 'jump to case label' compilation error if the control flow crosses the initialization.
**Action:** Always use a scoped block `{}` when declaring local variables inside `switch` cases to fix O(N^2) loops safely and avoid compilation errors.
