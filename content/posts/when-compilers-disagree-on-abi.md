---
title: "When Compilers Disagree on the ABI"
date: 2026-11-13
tags: ["libffi", "abi", "gcc", "clang", "x86-64"]
summary: "GCC and Clang compile the same function to two different calling conventions, and that breaks a promise libffi makes."
---

Someone reported a bug in libffi last week, and chasing it down led me somewhere I didn't expect: a function that GCC and Clang compile to two different ABIs. Both target x86-64 System V, and they still disagree.

Here's the function. It returns a one-element vector of a double:

```c
typedef double v1df __attribute__((vector_size(8)));  // <1 x double>

v1df f(long x) { v1df r; __builtin_memcpy(&r, &x, 8); return r; }
```

GCC returns it in memory. The caller passes a hidden pointer in `%rdi`, the real argument `x` lands in `%rsi`, and the function writes its result back through that pointer:

```asm
f:
	movq	%rdi, %rax
	movq	%rsi, (%rdi)
	ret
```

Clang returns it in `%xmm0`, with `x` staying in `%rdi` where you'd expect:

```asm
f:
	movq	%rdi, %xmm0
	retq
```

Nothing is wrong with either one. The x86-64 psABI says how `__m64` and `__m128` are passed, but it never defined the one-element `<1 x double>`. So each compiler picked something reasonable, and they picked differently. [The psABI folks know about it](https://gitlab.com/x86-psABIs/x86-64-ABI/-/issues/1). It just never got settled.

This is a problem for libffi. The whole point of the library is to call a function at runtime when all you have is its address and a description of its signature. To do that it has to know the calling convention: does this function hand me the result in `%xmm0`, or does it want a pointer and write the result to memory? For `<1 x double>` there are two right answers, and the function pointer doesn't tell me which compiler produced the code behind it. There's no bit that says "built with GCC."

So whatever libffi picks is correct for half the world and wrong for the other half. And wrong here isn't a slightly-off number. libffi was classifying this as `%xmm0`, so against a GCC-built function it passed `x` in `%rdi`, and the GCC code then wrote 8 bytes through `%rdi`. Through `x`. If your argument happened to be an address, libffi just stored a value there for you. That was the bug.

I can't guess the compiler, and I can't probe the function to find out, because the probe would be a real call with real side effects. The honest move is to stop pretending there's an ABI to match. On x86-64, libffi now rejects these shapes. The single-element float and double vectors, and the other sub-8-byte vectors GCC and Clang disagree on, come back as `FFI_BAD_TYPEDEF`. The real SIMD types, the 8- and 16-byte vectors the psABI actually specifies, still work.
