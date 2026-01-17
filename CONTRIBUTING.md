# Contributing

Thanks for contributing to bit_fields! Small, focused PRs are preferred.

## Pull requests

- Keep changes scoped and explain the motivation in the PR description.
- Add or update tests when changing behavior.
- Ensure formatting passes `clang-format` using the repo `.clang-format`.

## Development setup

```bash
cmake -B build -DBIT_FIELDS_BUILD_TESTS=ON -DBIT_FIELDS_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Formatting

```bash
find include tests examples -name '*.h' -o -name '*.hpp' -o -name '*.cpp' | \
  xargs clang-format -i
```

## Useful Make targets

```bash
make test
make asan-test
make coverage-report
```
