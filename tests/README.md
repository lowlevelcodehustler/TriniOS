# TriniOS Test Suite

## Overview

TriniOS uses a lightweight, self-contained unit test framework designed for host-native execution. Tests compile and run on the build host (Linux x86_64) without requiring the Mach kernel, QEMU, or any TriniOS runtime environment.

## Architecture

```
tests/
├── test_framework.h      # Test framework header (macros, runner, assertions)
├── test_framework.c      # Framework state (registry, counters)
├── Makefile              # Build & run all test suites
├── shell/
│   └── test_shell_parse.c  # Shell parser tests (14 tests)
├── mm/
│   └── test_vm_manager.c   # VM manager tests (17 tests)
└── fs/
    └── test_ext4.c         # ext4 filesystem tests (14 tests)
```

## Test Framework

The framework (`test_framework.h` / `test_framework.c`) provides:

### Macros
| Macro | Purpose |
|---|---|
| `TEST(name)` | Define a test function |
| `TEST_REGISTER(name)` | Register a test in main() |
| `TEST_SETUP(name)` | Define per-test setup function |
| `TEST_TEARDOWN(name)` | Define per-test teardown function |

### Assertions
| Macro | Purpose |
|---|---|
| `TEST_ASSERT(expr)` | Expression is true |
| `TEST_ASSERT_EQ(a, b)` | Values are equal |
| `TEST_ASSERT_NE(a, b)` | Values are not equal |
| `TEST_ASSERT_NULL(ptr)` | Pointer is NULL |
| `TEST_ASSERT_NOT_NULL(ptr)` | Pointer is not NULL |
| `TEST_ASSERT_STR_EQ(a, b)` | Strings are equal |
| `TEST_ASSERT_STR_NE(a, b)` | Strings are not equal |
| `TEST_ASSERT_MEM_EQ(a, b, n)` | Memory regions are equal |
| `TEST_ASSERT_IN_RANGE(v, lo, hi)` | Value within range |

### Design Decisions
- **Host-native execution**: Tests run on the build host, not in QEMU
- **Mocked dependencies**: Mach types, block devices, and kernel structures are mocked
- **No external test runner**: Framework is self-contained in one header + one .c file
- **Early exit on assertion failure**: Failed assertions `return` immediately from the test

## Running Tests

```bash
cd tests
make          # Build and run all test suites
make shell    # Run only shell parser tests
make mm       # Run only VM manager tests
make ext4     # Run only ext4 tests
make clean    # Remove build artifacts
```

## Test Suites

### Shell Parser Tests (14 tests)
Tests for `shell_parse_args()` and `shell_parse_line()` covering:
- Argument tokenization (single arg, empty lines)
- Null/invalid input handling
- Full line parsing (simple commands, background, redirects, pipes, complex)

### VM Manager Tests (17 tests)
Tests for virtual memory data structures and logic covering:
- VM space creation/destruction
- Region creation and management
- Region lookup by address (boundary conditions)
- Statistics calculation (pinned, GPU-accessible)
- Region type enum values
- Memory cleanup verification

### ext4 Filesystem Tests (14 tests)
Tests for ext4 on-disk structures with a RAM-backed mock block device covering:
- Superblock validation (magic, block size, state, inode count)
- Block device read/write (roundtrip, out-of-range)
- Inode validation (directory mode, block pointers)
- Directory entry parsing (., .., regular files)
- Block device consistency (100 block stress test)

## Adding New Tests

1. Create a new `.c` file in the appropriate subdirectory (`shell/`, `mm/`, `fs/`, etc.)
2. Include `"../test_framework.h"` and define tests using `TEST(name)`
3. Register tests in `main()` using `TEST_REGISTER(name)`
4. Add a build rule to the `Makefile`
5. Run `make` to verify

## Known Limitations

- `shell_parse_args()` has a shared-buffer implementation that doesn't correctly count multi-argument strings. The actual shell uses `shell_parse_line()` which handles this correctly.
- Tests require GCC (uses `__attribute__` conventions internally).
- No parallel test execution (runs sequentially).
- No test discovery (tests must be explicitly registered).

## Future Work

- Integration tests using QEMU
- BSD server tests (requires Mach type mocking)
- Shell builtin tests (requires mocking filesystem calls)
- Test coverage reporting
- Fuzzing for parser and ext4 structures