---
title: "Performance improvements in libffi"
date: 2026-06-20
tags: ["libffi", "ffi", "performance", "x86-64"]
summary: "libffi re-derives the same argument layout on every call. A new API lets a caller build that layout once and reuse it, several times faster, with no writable-executable memory."
---

libffi is a function call interpreter. You hand it a description of a function's signature at runtime, and it works out, on the spot, how to place each argument and make the call. It interprets the calling convention the way a bytecode VM interprets instructions. Nothing is compiled ahead of time, because the whole point is that you don't know the signature ahead of time.

An interpreter is not what you reach for when you want speed. The usual answer is to JIT: compile a bespoke call stub for each signature, native code that drops the arguments into their registers and jumps, with nothing left to interpret at runtime. It's quicker, but it gets there by writing fresh machine code into memory that's both writable and executable, which is exactly what modern systems are trying to stamp out.

So libffi stays an interpreter, on purpose. The question I set out to answer was how much faster it could get that way, by reusing what it already knows instead of generating code at runtime or mapping any page writable and executable.

## The waste

When you call a function through libffi, the work splits across two places. `ffi_prep_cif` runs once per signature. It classifies the whole thing, but it keeps only two results: the size of the stack frame the call will need, and a small code for how the return value comes back. The frame size has to be known before the call is built, because any argument that doesn't fit in a register spills to the stack, and that space is reserved up front. The return code is for afterward, because the result comes back in `rax`, or `xmm0`, or memory depending on the type, and something has to know where to read it from. Both are small and fixed-size, so they live in the `ffi_cif`. What prep throws away is the part it spent most of its time on: where each individual argument goes.

So on *every* `ffi_call`, the marshalling code walks the argument list again and re-derives that placement from scratch before copying the values into place. For a three-argument call on x86-64 that's around 650 instructions of bookkeeping, and it produces the identical answer every single time.

Most of those instructions aren't moving argument bytes. They're deciding where the bytes go. The System V AMD64 ABI classifies every argument by a fixed procedure, and running that procedure on a single argument means walking its type, recursing into a struct's fields and chasing the pointers in its type descriptor, sorting each 8-byte chunk into an INTEGER or SSE register class, and checking whether it still fits in the registers that are left or has to spill to the stack. That is branch-heavy, pointer-chasing work, the sort a CPU runs slowly, and it reruns on every call to compute a placement that never changes.

But function argument placement is a pure function of the signature. We can compute it once, remember it, and skip the work on every later call.

## A plan

The fix is a "plan": the placement compiled into a flat list of moves, a tiny bytecode for one signature. If `ffi_call` re-deriving the placement on every call is like interpreting a program by re-walking its syntax tree each time, the plan is the compiled bytecode: the tree-walk happens once, and every later call just runs the flat list. `build_plan` walks the argument types once, classifies each one the way the ABI rules say, and emits a move per piece: this 8-byte word goes in `rdi`, that 32-bit int gets sign-extended into `rsi`, this `double` lands in an SSE slot, that oversized thing spills to the stack. With the plan in hand, making the call is just running the moves. No re-classification.

![Building a call plan, then running it](plan-pipeline.svg)

The opcodes are deliberately dumb. `GP64` copies a word into a general register; `SE8`/`SE16`/`SE32` sign-extend a narrow int; `SSE64`/`SSE32` move a float; `STACK` memcpys a spilled argument. A three-argument call compiles to three or four of them. Here's what two real signatures turn into:

```
long (void *, void *, void *)    long (void *, int, void *)
  GP64  avalue[0] -> rdi           GP64  avalue[0] -> rdi
  GP64  avalue[1] -> rsi           SE32  avalue[1] -> rsi   (sign-extend)
  GP64  avalue[2] -> rdx           GP64  avalue[2] -> rdx
  => all GP64: thunk               => has an SE32: interpret
```

When every argument is a single 64-bit value in a general register, which is most pointer-passing code, the plan doesn't even need the interpreter. It's marked thunk-eligible, and a small hand-written thunk in `.text` loads the values straight from the argument array into the argument registers and calls. It skips the move loop, the intermediate register image, and the copying back and forth entirely. The call on the right keeps an `int`, so it needs the sign-extend, so it runs the move loop instead.

There's a subtlety in *running* the moves. The loop never loads an actual argument register, because C gives you no way to drop a value into `rdi` and hold it there across a call; the compiler owns the registers. So each move writes into a plain memory struct that mirrors the System V register file, the six integer registers and eight SSE registers laid out in order, and only once that image is built does a short assembly trampoline load every argument register from it in one shot and jump to the target. The C code moves bytes around in memory; the registers get their final values all at once, in `.text`, immediately before the call. That trampoline is the same one `ffi_call` has always used, so the plan changes when the placement is computed, not how the registers get loaded.

The plan is plain data, and the thunk ships in the binary's read-only text like any other function. Nothing is ever both writable and executable, the same property closures already get from [static trampolines](https://blog.lazym.io/2021/07/29/Cast-a-Closure-to-a-Function-Pointer-How-libffi-closure-works/).

## Build it once, invoke it many times

The plan is exposed as a small, opt-in API. You build a plan from a prepared `ffi_cif`, invoke it as many times as you like, and free it when you're done:

```c
ffi_call_plan *plan = ffi_call_plan_alloc(&cif);   /* build the plan once */

ffi_call_plan_invoke(plan, fn, &rv, av);           /* invoke it, no per-call setup */
/* ... invoke it again, and again ... */

ffi_call_plan_free(plan);
```

`ffi_call` itself is untouched. A binding that already caches an `ffi_cif` per signature, which is most of them, caches a plan beside it and calls through `ffi_call_plan_invoke`. The plan is immutable once built, so one plan can be shared and invoked from any thread without a lock. A signature the fast path can't handle is still fine: `invoke` falls back to `ffi_call` for it.

## The numbers

This is the fair comparison: one libffi, the same function, reached three ways. A plain direct call to it, the same call through `ffi_call`, and the same call through a prebuilt plan. Same binary, same machine (a Core Ultra 7 255H), same `-O2`, so the only thing that differs between the two FFI rows is the API. The timed loop is just this, over and over:

```c
ffi_type *at[] = { &ffi_type_pointer, &ffi_type_pointer, &ffi_type_pointer };
ffi_cif cif;
ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 3, &ffi_type_sint64, at);

ffi_call_plan *plan = ffi_call_plan_alloc(&cif);          /* built once */

void *av[] = { &a, &b, &c };
long rv;
ffi_call_plan_invoke(plan, (void(*)(void))fn, &rv, av);   /* <-- this is what we time */
```

```
ptr(p,p,p)                ns/call   vs a regular call
regular function call        1.9       1x
ffi_call_plan_invoke         5.1       2.7x
ffi_call                    31.0       16x
```

Calling that function the normal way through `ffi_call` costs about 16 times what a direct call to it costs. Through a prebuilt plan it's under 3 times. The plan is about 6x faster than `ffi_call`, and since it's the same library reached two ways, that gap is the API and nothing else.

Most of what the plan removes is the per-call re-classification: `ffi_call` rebuilds the placement every time, while `invoke` just runs the prebuilt moves. On this shape the plan takes the thunk, so it skips the register image too and lands close to a plain call: about 3 ns of FFI overhead on top of a 2 ns call, against 29 ns for `ffi_call`.

Mixed integer and floating-point signatures don't take the thunk, because a 32-bit `int` needs sign extension and a `double` needs an SSE register, so they run the move loop and land a little higher. They still skip the re-classification. A struct-by-value argument has no plan, so `invoke` falls back to `ffi_call` and costs exactly what it did before.

## Where the calls actually go

A 6x number on one shape only matters if real programs use that shape, and call it often enough that building a plan once pays off. So I traced one.

GNOME Shell is a good stress test: the entire desktop UI is JavaScript calling into C through GObject Introspection, which calls through libffi. I attached an eBPF uprobe to `ffi_call` with [Whistler](https://github.com/atgreen/whistler) and watched for a while. The top signatures looked like this:

```
21744  int   (void *)
19139  void *(void *, unsigned long)
13083  void *(void *)
10116  void  (void *, void *, void *, long, void *)
 9918  void *(void *, void *)
```

Around 90% of the calls are pure 64-bit-GP, pointers and longs, which is the thunk path. Not a single by-value struct argument showed up in over a hundred thousand calls. And these are the same handful of signatures called over and over, exactly the shape that rewards building a plan once and invoking it forever. A binding like GObject Introspection already holds an `ffi_cif` per signature; a plan slots in right beside it.

This all lives on the HEAD of the libffi git tree, not in any release, and it needs more testing before it's something to build on. The acceleration is x86-64 only, but the API is portable: everywhere else `ffi_call_plan_invoke` just calls `ffi_call`, so a binding can build a plan for every signature unconditionally and take the accelerated path where it exists, no `#ifdef` on its side. Whether the fast path is worth building for other ABIs isn't clear: the payoff is proportional to how much per-call classification there is to skip, and that varies a lot between calling conventions.

The code is on GitHub: [libffi](https://github.com/libffi/libffi).

---

*Edited for clarity after publishing: [`6fed8af`](https://github.com/atgreen/repl-yell/commit/6fed8af311fca8256b23673089da42e5beaf0cee).*
