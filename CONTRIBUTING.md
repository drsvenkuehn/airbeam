# Contributing to AirBeam

Thank you for your interest in contributing! Here's how to get started.

## Workflow

1. **Fork** the repository
2. Create a **feature branch**: `git checkout -b feature/my-feature`
3. Make your changes, following the code style below
4. Run unit tests: `cmake --build --preset msvc-x64-debug && ctest -R unit`
5. Submit a **pull request** to `main`

## Commit Messages

Use the conventional format:
```
type(scope): short description

Optional body.
```

Types: `feat`, `fix`, `test`, `docs`, `refactor`, `chore`

## Code Style

- C++17, MSVC 2022
- `#pragma once` on every header
- No heap allocation on Threads 3 or 4
- `CRITICAL_SECTION` not `std::mutex`
- All UI strings via `IDS_*` constants in `resources/resource_ids.h`
- Run `cmake --build --target check-locale-keys` to validate locale coverage

## Running Tests

```powershell
cmake --preset msvc-x64-debug
cmake --build --preset msvc-x64-debug
ctest --preset msvc-x64-debug -R unit --output-on-failure
```

## License

By contributing you agree that your contributions are licensed under the MIT License.
