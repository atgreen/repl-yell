---
title: "Whistler for Observability in High-Performance Computing"
date: 2026-04-10
tags: ["common-lisp", "ebpf", "whistler", "observability", "hpc"]
summary: "Whistler just landed its first real-world user — tracing syscalls on a HPC cluster for finance."
---

I build most of my Open Source projects for myself.  So I'm always
happy to learn when others find them useful.

I recently got a message from someone who'd found
[Whistler](https://github.com/atgreen/whistler) via Hacker News and
immediately put it to work.  And the use case is
interesting... observability tooling in their HPC cluster.  This HPC
cluster is being used in a proprietary trading firm, where they are
trying to better understand the performance characteristics of their
applications, especially as they relate to the storage layer.

Their previous workflow was: write the eBPF program in C, build LLVM
and libbpf and bpftool to produce a BPF skeleton header, write a C
wrapper to expose the skeleton's static functions as a shared library,
then write the actual userspace logic in Common Lisp with a pile of
CFFI boilerplate.  That's a lot of ceremony for what amounts to
"count syscalls per process and tag them by job ID."

With Whistler, all of that collapses into pure Lisp.  No LLVM, no
skeleton headers, no C wrappers, no CFFI boilerplate.  They're
tracking per-process syscall counts, tracing execs and forks to map
PIDs to commands, and tying short-lived processes back to long-running
jobs through cgroup membership.

I'd love to hear about other use cases.  Please reach out!
