---
title: "Whistler: Live eBPF Programming from the Common Lisp REPL"
date: 2026-03-22
tags: ["common-lisp", "ebpf", "compilers", "observability", "linux"]
summary: "Writing, compiling, loading, and querying eBPF programs in one Lisp form."
---

A string of recent experiments around observability and security for
agentic AI systems led me down the eBPF rabbit-hole. When I emerged,
I came back with a full optimizing compiler for a Common Lisp-based
DSL for eBPF called [Whistler](https://github.com/atgreen/whistler).

Whistler lets you write shorter code, with less ceremony than eBPF C
code, and still produce highly-optimized eBPF output, equivalent or
better than clang. And Whistler generates those ELF eBPF files
directly, without any of the eBPF clang+llvm toolchain.

In addition to generating object code files directly, and loading them
in the traditional way, you can actually inline Whistler code directly
in your Common Lisp programs and have them compiled/loaded/unloaded as
part of your traditional REPL process, where no object file even lands
on disk.

### A taste

Here's a kprobe that counts every `execve` call on the system:

```lisp
(with-bpf-session ()
  (bpf:map counter :type :hash :key-size 4 :value-size 8 :max-entries 1)
  (bpf:prog trace (:type :kprobe
                    :section "kprobe/__x64_sys_execve"
                    :license "GPL")
    (incf (getmap counter 0))
    0)
  (bpf:attach trace "__x64_sys_execve")
  (loop (sleep 1)
        (format t "execve count: ~d~%" (bpf:map-ref counter 0))))
```

That's a complete, runnable program. The `bpf:prog` body compiles to
eBPF bytecode *during macroexpansion*. The bytecode is embedded as a
literal in the expansion. At runtime, the map is created, the program
is loaded into the kernel, and the probe is attached. The `loop` at
the bottom is plain Common Lisp, polling the map every second.

### A real-world example

Here's something more substantial — a uprobe that traces every
`ffi_call` invocation in libffi, counting calls by program name and
function signature:

```lisp
(with-bpf-session ()
  ;; BPF side — compiled to bytecode at macroexpand time
  (bpf:map stats :type :hash :key-size 40 :value-size 8 :max-entries 10240)

  (bpf:prog ffi_call_tracker (:type :kprobe
                               :section "uprobe/ffi_call"
                               :license "GPL")
    (let ((cif (make-ffi-cif))
          (ft  (make-ffi-type))
          (key (make-stats-key)))
      (probe-read-user cif (sizeof ffi-cif) (pt-regs-parm1))
      (probe-read-user ft (sizeof ffi-type) (ffi-cif-rtype cif))
      (setf (stats-key-rtype key) (ffi-type-type-code ft)
            (stats-key-abi key)   (ffi-cif-abi cif)
            (stats-key-nargs key) (ffi-cif-nargs cif))
      (get-current-comm (stats-key-comm-ptr key) 16)
      (memset key 16 #xFF 16)
      (do-user-ptrs (atype-ptr (ffi-cif-arg-types cif)
                               (ffi-cif-nargs cif) +max-args+ :index i)
        (probe-read-user ft (sizeof ffi-type) atype-ptr)
        (setf (stats-key-arg-types key i) (ffi-type-type-code ft)))
      (incf (getmap stats key)))
    0)

  ;; Userspace side — normal CL code, runs at runtime
  (bpf:attach ffi_call_tracker "/lib64/libffi.so.8" "ffi_call")
  (format t "Tracing ffi_call. Press Ctrl-C to dump stats.~%")

  (handler-case (loop (sleep 1))
    (sb-sys:interactive-interrupt ()
      ;; Iterate the map and print results
      ...)))
```

The output:

```
Compiling and loading BPF program...
Attaching uprobe to ffi_call in /lib64/libffi.so.8...
Tracing ffi_call. Press Ctrl-C to dump stats.
^C
COUNT       COMM              SIGNATURE
--------    ----------------  ---------
       880  ghostty           void(ptr, ptr, u32) [unix64]
       384  gnome-shell       void(ptr, ptr, u32) [unix64]
       352  gnome-shell       void(ptr, ptr, ptr, s64, ptr) [unix64]
       224  gnome-shell       void(ptr, ptr) [unix64]
       176  ghostty           void(ptr, ptr) [unix64]
       ...
```

Everything happened inside one SBCL process. The BPF bytecode never
touched disk.

### How it works

The `bpf:` prefix is the boundary between kernel and userspace.
Forms prefixed with `bpf:` are declarations for the BPF compiler:

- `bpf:map` — declares a BPF map (compiled at macro-expansion time)
- `bpf:prog` — declares a BPF program (compiled at macro-expansion time)
- `bpf:attach` — generates `perf_event_open` calls (runs at runtime)
- `bpf:map-ref` — generates `bpf_map_lookup_elem` calls (runs at runtime)

Everything else is normal Common Lisp. The boundary is syntactic, not
semantic — both sides share the same Lisp image.

The key insight: the Whistler compiler runs during macroexpansion. By
the time SBCL compiles the `with-bpf-session` form, the eBPF bytecode
is already a constant — embedded as a literal byte array in the
expansion. The runtime code just creates maps, patches FD relocations,
and calls `bpf(BPF_PROG_LOAD, ...)`. And because it all runs during
macroexpansion, you get compile-time errors with context:

```
  error: narrow type U8 passed as pointer to PROBE-READ
  in: (PROBE-READ EVENT 8 PTR)
  expected: a u64 pointer value
  hint: U8 values are 0-255, not valid pointers — use (load u64 ...) to read
```

### One struct, both sides

`whistler:defstruct` generates accessors for both BPF and CL:

```lisp
(whistler:defstruct stats-key
  (comm (array u8 16))
  (arg-types (array u8 16))
  (nargs u16)
  (rtype u8)
  (abi u8)
  (pad u32))
```

On the BPF side, this gives you `(make-stats-key)`, `(stats-key-rtype ptr)`,
`(setf (stats-key-rtype ptr) val)` — stack allocation and direct load/store
with compile-time offsets.

On the CL side, it generates `stats-key-record` (a CL `defstruct`),
`decode-stats-key` (bytes → struct), and `encode-stats-key` (struct →
bytes). The userspace map iteration code uses the same field names:

```lisp
(let ((key (decode-stats-key raw-bytes)))
  (stats-key-record-nargs key)   ;; → 3
  (stats-key-record-rtype key)   ;; → 0
  (stats-key-record-comm key))   ;; → #(112 121 116 104 111 110 51 0 ...)
```

No manual byte-offset parsing. One definition serves both kernel and
userspace.

### The kernel at your fingertips

Whistler can import definitions directly from the running kernel.
`deftracepoint` reads tracepoint format files from tracefs:

```lisp
(deftracepoint sched/sched-switch prev-pid prev-state next-pid)
;; Generates: (tp-prev-pid) → (ctx-load u32 24)
;;            (tp-prev-state) → (ctx-load u64 32)
```

`import-kernel-struct` reads the kernel's BTF:

```lisp
(import-kernel-struct task_struct pid tgid flags)
;; Generates: (task-struct-pid ptr) → (load u32 ptr 2768)
;;            +task-struct-size+ → 9856
```

Offsets resolve from your running kernel at compile time — kernel
headers and vmlinux.h are unnecessary.

### The loader is pure CL too

`whistler/loader` is a complete BPF userspace loader written in Common
Lisp with zero C dependencies. It uses SBCL's `sb-alien` for direct
syscall access:

- ELF parser for `.bpf.o` files
- BPF map creation and operations (lookup, update, delete, iterate)
- Map FD relocation patching
- Program loading with verifier error capture
- Kprobe, uprobe, and XDP attachment
- Ring buffer consumer via mmap + epoll

For the file-based workflow:

```lisp
(with-bpf-object (obj "my-probes.bpf.o")
  (attach-obj-kprobe obj "trace_execve" "__x64_sys_execve")
  ...)
```

Or skip the file entirely with `with-bpf-session`.

### Polyglot userspace

Not everything has to be Lisp. If you prefer to write the userspace
side in Go, C, Rust, or Python, Whistler can generate matching struct
definitions from the same `defstruct` declarations used in the BPF
program:

```sh
whistler compile probes.lisp --gen c go rust python
```

This produces header files with struct layouts guaranteed to match the
BPF side, since both are derived from the same source. You compile the
BPF with Whistler and write the loader in whatever language your team
already uses.

### Permissions without root

You don't need root. Grant capabilities to SBCL:

```sh
sudo setcap cap_bpf,cap_perfmon+ep /usr/bin/sbcl
```

Now `sbcl --load my-bpf-program.lisp` works as your regular user.
Tracepoint format files need `chmod a+r` to allow non-root
compilation with `deftracepoint`.

### Why this matters

The traditional eBPF workflow is: write C for the BPF side, compile
with clang, then write Go or Rust or Python for the userspace side.
Two languages, separate build steps, multiple processes.

With Whistler 1.0, the workflow is: write Lisp. The compiler, loader,
and userspace application share a process. You can develop at the REPL —
modify a probe, re-eval the form, see results immediately. The feedback
loop is instant.

Check it out on [GitHub](https://github.com/atgreen/whistler).
