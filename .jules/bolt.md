## 2024-05-24 - [Avoid committing temporary files]
**Learning:** Even if the code optimization is flawless, committing temporary testing and benchmarking scripts (and their compiled binaries) into the repository is a strict blocking issue for PRs.
**Action:** Always delete temporary files and compiled binaries used for testing or benchmarking before moving on to the pre-commit step or submitting a PR.
