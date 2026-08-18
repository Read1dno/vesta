# Contributing

Keep changes focused, reproducible and reviewable.

1. Create a branch from `main`.
2. Build the Release preset and run the tests.
3. Keep process access external and read-only.
4. Do not add credentials, private dumps, generated build trees or proprietary
   assets.
5. Preserve third-party copyright and license notices.
6. Explain observable behavior changes in the pull request.

Before submitting:

```powershell
cmake --preset release
cmake --build --preset release --parallel
ctest --test-dir build/release -C Release --output-on-failure
node website/build.mjs
```
