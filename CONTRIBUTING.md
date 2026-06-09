# Contributing to PulsePad GTK

## Configure, build, and test

Normal local build:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Dependency-light logic/test build:

```bash
cmake -S . -B build -DBUILD_APP=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Build without MIDI:

```bash
cmake -S . -B build -DPULSEPAD_ENABLE_MIDI=OFF
cmake --build build
```

Strict build for CI-style checks:

```bash
cmake -S . -B build-strict \
    -DBUILD_TESTING=ON \
    -DPULSEPAD_ENABLE_WARNINGS=ON \
    -DPULSEPAD_WARNINGS_AS_ERRORS=ON
cmake --build build-strict
ctest --test-dir build-strict --output-on-failure
```

Sanitizer build:

```bash
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DPULSEPAD_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

## Formatting and static analysis

Check formatting:

```bash
cmake --build build --target format-check
```

Apply formatting:

```bash
cmake --build build --target format
```

Run clang-tidy on dependency-light sources:

```bash
cmake --build build --target tidy
```

Do not reformat unrelated files as part of feature or bug-fix changes.

## Install test

```bash
cmake --install build --prefix /tmp/pulsepad-install
```

This installs the executable, desktop entry, AppStream metadata, and icon under the chosen prefix.
