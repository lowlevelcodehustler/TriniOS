# GNU Mach integration patches (TriniOS)

Durability export of the TriniOS-specific changes made to the GNU Mach
(`kernel/gnumach` submodule). The fork commit exists **only on the dev box**
(WSL home snapshot); it is on no remote, and the TriniOS repo deliberately
does not record its gitlink pointer (a public clone would get a broken
submodule). This directory makes the integration reproducible from the
TriniOS tree alone.

## Contents

| File | What it is |
|---|---|
| `trini-integration.diff` | Full diff of fork commit `3ba5df9e` ("Minor tinkering in foolish way to integrate userspace with kernel (AI-gen)"), generated with `git show 3ba5df9e`. Purely additive: +1592/−0 across 9 files. |
| `Makefrag.trini` | The TriniOS build fragment as committed in the fork (`git show 3ba5df9e:Makefrag.trini`). Included by gnumach's `Makefile.am` to compile four TriniOS kernel-extension sources into the Mach kernel library. |
| `trini_bootstrap.patch` | Reference copy of the minimal upstream-file change (the guarded `trini_bootstrap()` hook in `kern/startup.c`) as it was hand-maintained before being folded into commit `3ba5df9e`. Kept for documentation; do not apply on top of the full diff. |

## What the integration actually does

Only **two upstream files** are modified by the fork:

1. **`Makefrag.am` (+3 lines)** — includes `Makefrag.trini` so the TriniOS
   extension sources get built into the kernel library.
2. **`kern/startup.c` (+10 lines)** — after `bootstrap_create()` and before
   user-space servers start, calls `trini_bootstrap()` when compiled with
   `-DTRINI_OS` (defined by `Makefrag.trini`). The hook is fully guarded;
   an unmodified gnumach build is unaffected.

Everything else in the diff is new files:

- **Meaningful:** `Makefrag.trini` (build fragment) and
  `tests/test-multiboot` (multiboot test script).
- **Hygiene junk committed by mistake** (should not be carried forward):
  `Makefrag.am.trini.bak`, `configure.ac.trini.bak`,
  `kern/startup.c.trini.bak` (editor backups), `config.status.rej`
  (a failed patch hunk from a rejected configure change), and
  `i386/i386asm.symc` (a generated file).

## How to apply on a fresh gnumach checkout

The diff is against the fork commit's parent, but none of the 12 upstream
commits between the recorded submodule pointer (`e56301bd`) and the fork
touch `Makefrag.am` or `kern/startup.c`, so it applies cleanly at either:

```sh
git clone <gnumach-remote> gnumach && cd gnumach
git checkout e56301bd            # TriniOS's recorded submodule pointer
# (optionally fast-forward through the 12 upstream commits first)
patch -p1 < /path/to/kernel/gnumach-patches/trini-integration.diff
```

or, if you have access to the dev box's fork:

```sh
git fetch <devbox> trini-integration   # branch created at 3ba5df9e
git checkout trini-integration
```

## Known issues (fix before any distribution)

- **Hardcoded path:** `Makefrag.trini` sets
  `TRINI_ROOT := /root/TriniOS/kernel`. Parameterize it (e.g. an
  autoconf option or a relative path) before the build is shared.
- **Junk in the commit:** the `.bak` files, `config.status.rej`, and the
  generated `i386asm.symc` should be removed from the fork history (or at
  least not re-committed).
- **GPL copyleft exposure:** gnumach is GPLv2+. Building TriniOS sources
  (`task/task_manager.c`, `host/host_local.c`, `mm/vm_manager.c`,
  `mm/gpu_mem.c`) into the Mach kernel library makes them part of a GPL
  derivative at distribution time. Fine for private use; decide licensing
  before distributing TriniOS binaries that embed these objects.

## Provenance

- Fork commit: `3ba5df9e` on branch `trini-xpm` (also tagged as branch
  `trini-integration`) in the dev box's gnumach worktree.
- Recorded submodule pointer in TriniOS master: `e56301bd`.
- Exported from the TriniOS Windows snapshot; regenerate with
  `git -C kernel/gnumach show 3ba5df9e > trini-integration.diff` if the
  fork ever changes.
