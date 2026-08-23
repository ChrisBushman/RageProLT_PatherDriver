---
name: panther-kext-build-test
description: Build and test a Mac OS X 10.3/10.4 PPC kext and userspace tools with the period gcc 3.3 toolchain, and run the edit-build-test loop against real vintage hardware over SSH. Use when compiling/linking a Panther-era kext, cross-host syncing, or structuring hardware tests that require root and can panic.
---

# Panther (10.3/10.4) PPC kext build & test workflow

## Build the kext with the period toolchain (gcc 3.3), on the target

Modern clang cannot produce a loadable 10.3 PPC kext. Build on the vintage
machine itself (or a matching toolchain). Flags that matter:

```make
# C++ kext objects
KCXX := -static -mlong-branch -fno-rtti -fno-exceptions -fcheck-new \
        -fno-builtin -fapple-kext
KDEF := -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
KINC := -I$(KFRAME)/Headers -I$(KFRAME)/PrivateHeaders   # Kernel.framework
# link: -static -r, pull in the C++ static-ctor/OSMetaClass glue
ld -static -r -o $(EXEC) $(OBJS) -lkmodc++ -lcc_kext
```

Why each:
- `-static -mlong-branch`: non-PIC codegen + long call sequences so branches to
  kernel symbols don't overflow the PPC 24-bit displacement at kld load time
  (and so `ld -static -r` doesn't reject PIC stub sections).
- `-fapple-kext`: correct C++ ABI / vtable layout for a loadable kext under
  gcc 3.3.
- `-fno-rtti -fno-exceptions -fno-builtin`: no C++ runtime in the kernel.
- Do NOT `-nostdinc`: the Kernel.framework omits the arch base headers
  (`ppc/ansi.h` etc.) that gcc's `/usr/include` supplies — let both be found.
- A KMOD glue file (`kmod_glue.c`, `KMOD_EXPLICIT_DECL`) provides the module
  start/stop and version.

Add a fast `-fsyntax-only` target for header/API checks, and a
`kextload -n -t -z` "validate WITHOUT loading" target to catch personality/link
problems with zero panic risk before any real load.

`-nostdinc` note for shared headers: a header included by both kext and
userspace should guard `#include <stdint.h>` with `#if !defined(KERNEL)` — the
kext build has no libc stdint.

## Toolchain hazards specific to old hosts

- **Never build the PPC tools on a modern host.** A stray host build produces
  x86_64/arm64 binaries that, once synced, overwrite the PPC ones →
  "cannot execute binary file". Build tools ONLY on the target; keep host build
  artifacts out of the sync (gitignore them).
- Host clang will emit a flood of false-positive diagnostics on kext/IOKit
  sources (no 10.3 SDK locally). Ignore them; trust the on-target build result.
- On the target, userspace IOKit types like `io_connect_t` come from
  `<IOKit/IOKitLib.h>`, not `<mach/mach.h>` — include IOKitLib in headers that
  use them so include order doesn't matter.

## Cross-host sync: gnutar pipe, not rsync

Panther ships rsync 2.6, incompatible with a modern host's rsync 3.x (silent
failures/protocol errors). Sync with a tar pipe instead:

```sh
tar czf - <files> | ssh target 'cd dest && tar xzf -'
```

**Verify every sync landed.** Syncs have silently truncated (clock skew /
SIGPIPE). After `make sync`, `grep` a known-new string in the file on the target
before you trust it. Skipping this cost a whole debugging cycle chasing a "fix"
that never arrived on the box.

## The hardware test loop (root + panic risk)

Structure so the human operator runs anything requiring root:

1. Edit on host → `make sync` → **grep-verify on target** → build on target.
2. Hand the operator a single command, e.g.
   `ssh -t target 'sudo sh .../<name>-test.sh'`. The script stages the kext
   (`cp` + `chown root:wheel`), `kextload`, runs the test tool, `kextunload`.
3. Operator pastes output into a file you can read back
   (`ssh target cat ... | textutil -convert txt -stdout` for .rtf).
4. On panic, operator photographs the screen; read the photo for PC/LR + trace.

Make destructive tools **dry-run by default** and require an explicit `go`
argument to actually touch DMA/hardware, with a hard range check on any address
before it reaches the engine.
