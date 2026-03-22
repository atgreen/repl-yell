---
title: "Whistler: Live eBPF Programming from the Common Lisp REPL"
date: 2026-03-22
tags: ["common-lisp", "ebpf", "compilers", "observability", "linux"]
summary: "Writing, compiling, loading, and querying eBPF programs in one Lisp form — no files, no build step, no C."
---

What if you could write a kernel probe at the REPL, attach it to a
running system, and query the results — all without leaving your Lisp
session?

[Whistler](https://github.com/atgreen/whistler) started as an eBPF
compiler. It's now an entire platform: compiler, loader, and inline
session runtime, all in pure Common Lisp. Version 1.0 ships with
`with-bpf-session`, a macro that compiles BPF code at macroexpand
time, loads it into the kernel at runtime, and gives you live access
to maps and events from your REPL.

### The whole stack in one form

Here's a complete program that attaches a uprobe to `ffi_call` in
libffi, counts calls by signature, and dumps stats when you hit
Ctrl-C:

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

When you load this file, here's what happens:

1. SBCL reads the `with-bpf-session` form
2. The macro expander compiles the `bpf:prog` body to eBPF bytecode at
   macroexpand time — the Whistler compiler runs inside the expander
3. The bytecode is embedded as a literal array in the macro expansion
4. At runtime: maps are created, the program is loaded into the kernel,
   the uprobe is attached
5. The CL code runs: sleep/poll loop, Ctrl-C handler, map iteration

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

No `.bpf.o` file was created. No C was compiled. No Go loader was
written. One Lisp file, one language, one process.

### How it works

The `bpf:` prefix is the boundary between kernel and userspace.
Forms prefixed with `bpf:` are declarations for the BPF compiler:

- `bpf:map` — declares a BPF map (compiled at macro-expansion time)
- `bpf:prog` — declares a BPF program (compiled at macro-expansion time)
- `bpf:attach` — generates `perf_event_open` calls (runs at runtime)
- `bpf:map-ref` — generates `bpf_map_lookup_elem` calls (runs at runtime)

Everything else is normal Common Lisp. The boundary is syntactic, not
semantic — both sides share the same Lisp image.

The compiler runs during macroexpansion, which means you get
compile-time errors with context:

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

No kernel headers, no vmlinux.h, no CO-RE ceremony. Offsets resolve from
your running kernel at compile time.

### The loader is pure CL too

`whistler/loader` is a complete BPF userspace loader written in Common
Lisp — no libbpf, no CFFI, no C dependencies. It uses SBCL's `sb-alien`
for direct syscall access:

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

### Permissions without root

You don't need root. Grant capabilities to SBCL:

```sh
sudo setcap cap_bpf,cap_perfmon+ep /usr/bin/sbcl
```

Now `sbcl --load my-bpf-program.lisp` works as your regular user.
Tracepoint format files need `chmod a+r` to allow non-root
compilation with `deftracepoint`.

### Why this matters

The traditional eBPF workflow is: write C, compile with clang, load
with a separate binary (Go, Rust, Python), read maps from that binary.
Four languages, three build steps, multiple processes.

With Whistler 1.0, the workflow is: write Lisp. The compiler, loader,
and userspace application share a process. You can develop at the REPL —
modify a probe, re-eval the form, see results immediately. The feedback
loop is instant.

eBPF is the most powerful observability and security mechanism in Linux.
The tooling shouldn't be harder than the ideas. With Whistler, it isn't.

The code is on [GitHub](https://github.com/atgreen/whistler).
