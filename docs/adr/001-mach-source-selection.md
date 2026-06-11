# ADR-001: Mach Source Selection

**Status:** Accepted
**Date:** 2026-06-11
**Decision:** Use GNU Mach (gnumach) as the base microkernel

---

## Context

The TriniOS build plan (Phase 0) requires selecting a Mach microkernel source base. The original plan considered:

1. **Mach 3.0 (CMU)** — Original Mach 3.0 from Carnegie Mellon University
2. **XNU Mach subsystem** — Apple's extraction from macOS kernel
3. **GNU Mach (gnumach)** — GNU project's Mach distribution with x86_64 support

## Options Evaluated

| Option | License | x86_64 | Status | Verdict |
|---|---|---|---|---|
| **Mach 3.0 (CMU)** | MIT/BSD | i386 only | 1993-era source, no native x86_64 | Rejected — requires full x86_64 port |
| **XNU Mach** | Apache 2.0 | Yes | Modern but Apple-entangled | Rejected — Apple-specific code/IP concerns |
| **GNU Mach** | GPL3 | Yes | Actively maintained, x86_64 supported | **Selected** |

## Decision

**GNU Mach** was selected as the base microkernel for TriniOS.

### Rationale

1. **x86_64 Support**: GNU Mach has a native `x86_64/` architecture directory with full 64-bit support. Mach 3.0 only targets i386 (32-bit), which would require a significant porting effort before any TriniOS-specific work could begin.

2. **Clean License**: GNU Mach is GPL3 licensed. While more restrictive than BSD/MIT, it is well-understood and compatible with our open-source goals. (Note: We may need to evaluate if GPL3 is compatible with our planned BSD 3-Clause core license — see Open Questions.)

3. **Active Maintenance**: GNU Mach is maintained by the GNU Hurd project with regular commits. The codebase includes modern compiler compatibility fixes and security improvements not present in the 1993 Mach 3.0 source.

4. **Build System**: Uses standard autotools (configure/make), making it straightforward to build on modern Linux systems. No custom ODE/odemake toolchain required.

5. **QEMU Compatibility**: Successfully boots in QEMU x86_64 with standard multiboot protocol.

### Verification

- Source cloned from: `git://git.savannah.gnu.org/hurd/gnumach.git`
- Built with: `./configure --host=x86_64-linux-gnu --enable-kdb`
- Kernel image: `gnumach` (ELF 64-bit, 3.6MB, statically linked)
- QEMU boot test: **PASS** (kernel loads, runs without crash)

## Consequences

### Positive
- Immediate x86_64 support — no porting work needed
- Modern build system — standard autotools
- QEMU-ready — can proceed to Phase 1 immediately
- Active community and bug tracking via GNU Savannah

### Negative / Risks
- GPL3 license may require TriniOS core to also be GPL3 (license compatibility review needed)
- Includes some Hurd-specific code that may need cleanup for TriniOS
- x86_64 support marked as "in progress" in README — some features may be incomplete

## Open Questions

1. **License Compatibility**: Does GPL3 on the kernel require the entire TriniOS to be GPL3? Need to review microkernel/BSD server boundary. The microkernel itself being GPL3 may not infect user-space servers that communicate via IPC.

2. **Hurd-Specific Code**: How much Hurd-specific integration exists in the kernel? Need to audit for dependencies on Hurd-specific interfaces.

3. **aarch64 Port**: GNU Mach includes an `aarch64/` directory. Quality and completeness of ARM64 support needs evaluation for Phase 7.

## References

- [GNU Mach Repository](https://git.savannah.gnu.org/cgit/hurd/gnumach.git/)
- [GNU Mach README](https://www.gnu.org/software/hurd/microkernel/mach/gnumach.html)
- [Build Plan Section 3.4](../TriniOS_Build_Plan.md#34-mach-variant-selection)