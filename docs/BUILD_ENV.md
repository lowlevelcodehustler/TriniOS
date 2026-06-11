# Build Environment

## Host System
- **OS**: Arch Linux (container)
- **GPU**: NVIDIA GeForce RTX 4050 (via NVIDIA Container Toolkit / CDI)

## Installed Toolchain

### Core Build Tools
| Tool | Package | Purpose |
|---|---|---|
| GCC 16.1.1 | `gcc` | C/C++ compiler |
| Make 4.4.1 | `make` | Build system |
| CMake 4.3.3 | `cmake` | Build system |
| Ninja 1.13.2 | `ninja` | Fast build system |
| GDB 17.2 | `gdb` | Kernel debugging |
| Binutils 2.46 | `binutils` | Linker, assembler |
| Flex 2.6.4 | `flex` | Lexer generation |
| Bison 3.8.2 | `bison` | Parser generation |

### Kernel Development Dependencies
| Tool | Package | Purpose |
|---|---|---|
| libelf | `libelf` | ELF file manipulation |
| GMP | `gmp` | Arbitrary precision arithmetic |
| MPFR | `mpfr` | Floating-point arithmetic |
| MPC | `mpc` | Complex number arithmetic |
| Python 3.14 | `python` | Build scripts, QEMU tools |
| Perl 5.42 | `perl` | Build system deps |

### Virtualization
| Tool | Package | Purpose |
|---|---|---|
| QEMU (full) | `qemu-full` | Kernel emulation/testing (x86_64, ARM, etc.) |

### NVIDIA / CUDA Stack
| Component | Version | Purpose |
|---|---|---|
| CUDA Toolkit | 13.3 | GPU programming toolkit |
| cuDNN | 9.23.1 | Deep neural network library |
| NCCL | 2.30.7 | Multi-GPU communication |
| OpenCL (NVIDIA) | 610.43.02 | OpenCL driver/I CD |
| NVIDIA Container Toolkit | 1.19.1 | GPU passthrough in container |

**CUDA Path**: `/opt/cuda/`
**CUDA Bin**: `/opt/cuda/bin/` (add to PATH via `source /etc/profile.d/cuda.sh`)

## CUDA Environment Setup

```bash
# Source CUDA environment
source /etc/profile.d/cuda.sh
export PATH="/opt/cuda/bin:$PATH"
export LD_LIBRARY_PATH="/opt/cuda/lib:$LD_LIBRARY_PATH"

# Verify
nvcc --version  # CUDA 13.3
nvidia-smi     # GPU info (if CDI configured)
```

## GPU Access (Container)

This build host runs in a Docker container. GPU access requires:
1. **NVIDIA Container Toolkit** installed on host
2. **`--gpus all`** or **`--gpus device=0`** flag when running container
3. **CDI (Container Device Interface)** for device exposure

## Cross-Compilation Target

| Target Arch | Tool Prefix | Notes |
|---|---|---|
| x86_64 (native) | — | Primary build target |
| x86_64 (cross) | `x86_64-elf-` | For Mach kernel (no libc deps) |
| ARM64 (future) | `aarch64-elf-` | Apple Silicon support (Phase 7) |

## Git Repository

- **Local repo**: `/root/TriniOS/.git/`
- **Branch**: `master`
- **Build plan**: Protected by `.gitignore` (not committed to repo)

## References

- [Mach 3.0 Source](ftp://ftp.cix.ucla.edu/public/Mach/)
- [GNU Mach](https://www.gnu.org/software/gnu-mach/)
- [llama.cpp](https://github.com/ggerganov/llama.cpp) (target LLM inference engine)
- [QEMU Documentation](https://www.qemu.org/docs/)