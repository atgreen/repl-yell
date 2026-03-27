---
title: "cl-kawa: A Turducken of Programming Languages"
date: 2026-02-21
tags: ["common-lisp", "scheme", "java", "kawa", "openldk", "jvm", "lisp"]
summary: "cl-kawa nests Scheme inside Java inside Common Lisp: three language runtimes in a single SBCL process, no FFI or external JVM required."
---

A [turducken](https://en.wikipedia.org/wiki/Turducken) is a chicken stuffed inside a duck stuffed inside a turkey. [cl-kawa](https://github.com/atgreen/cl-kawa) is the programming language equivalent: [Kawa Scheme](https://www.gnu.org/software/kawa/) running on a Java runtime running inside Common Lisp. Three language runtimes nested in a single SBCL process, sharing one heap, with no FFI, no sockets, and no external JVM.

### The Layers

Here's how the turducken is assembled:

**Common Lisp (the turkey)**, the outermost layer. SBCL is the host process. It provides the REPL, the compiler, CLOS, conditions, and everything else you'd expect from a Common Lisp environment.

**Java (the duck)**, the middle layer. My [OpenLDK](https://github.com/atgreen/openldk) project is a JIT compiler and runtime for Java written entirely in Common Lisp. It reads `.class` and `.jar` files and transpiles Java bytecode into Common Lisp code in memory. Java classes become CLOS classes. Java exceptions become Common Lisp conditions. No external JVM required. The entire Java runtime lives inside the Lisp image.

**Kawa Scheme (the chicken)**, the innermost layer. [Kawa](https://www.gnu.org/software/kawa/) is a Scheme implementation from the GNU Project created by Per Bothner, who I had the pleasure of working with back at Cygnus Solutions. Kawa compiles Scheme to Java bytecode. Normally it runs on a standard JVM, but here it runs on OpenLDK's Common Lisp JVM instead.

The execution chain: Scheme source is compiled by Kawa to Java bytecode, which OpenLDK transpiles to Common Lisp, which SBCL compiles to native x86-64 machine code. All within one process.

### Using It

```lisp
(asdf:load-system :cl-kawa)

;; Initialize the runtime
(kawa:startup :classpath "libs/kawa-3.1.1.jar")

;; Evaluate Scheme expressions
(kawa:eval '(+ 1 2))                  ; => 3
(kawa:eval '(string-length "hello"))   ; => 5
(kawa:eval '(list 1 2 3))             ; => (1 2 3)
(kawa:eval "(* 6 7)")                  ; => 42
```

The `kawa:eval` function accepts both s-expressions and strings. S-expressions are serialized to Scheme source, evaluated by Kawa, and the results are converted back to Common Lisp values.

You can also look up Scheme procedures and call them directly from Common Lisp:

```lisp
(let ((add (kawa:lookup "+")))
  (kawa:funcall add 10 20))            ; => 30
```

### Crossing the Boundary in Both Directions

The interop isn't one-way. You can register Common Lisp functions so Scheme code can call them:

```lisp
(kawa:register "cl-square" (lambda (x) (* x x)))
(kawa:eval '(cl-square 7))            ; => 49
```

Under the hood, `kawa:register` creates a CLOS class that subclasses Kawa's `gnu.mapping.Procedure`, wrapping the Common Lisp lambda so Kawa's evaluator can invoke it directly.

And here's the hello world that touches all three languages:

```lisp
(format t "~A~%"
  (kawa:eval '(let ((s (|java.lang.String|
                         (string-append "Hello" ", " "World!"))))
               (|s:toUpperCase|))))
;; prints: HELLO, WORLD!
```

Common Lisp calls `kawa:eval`. Kawa Scheme assembles the string with `string-append` and wraps it in a `java.lang.String`. Java's `String.toUpperCase()` executes, its bytecode having been transpiled by OpenLDK into Common Lisp, then compiled by SBCL to native assembly. The result flows back through Kawa into Common Lisp and is printed by `format`.

### Why?

The honest answer: because I could. My OpenLDK project already gave me a Java runtime inside Common Lisp. Kawa already compiles Scheme to Java bytecode. Connecting the two was a matter of writing the glue: about 350 lines of Common Lisp for startup, s-expression serialization, value conversion, and the procedure bridge.

But Kawa also exercises OpenLDK in ways that most Java programs never would, which makes it a fantastic stress test.

**Runtime class generation.** Kawa compiles every REPL expression, every module, and every lambda into a new JVM class on the fly. It uses its own bytecode library (`gnu.bytecode`) to build class files as byte arrays in memory, then loads them via `ClassLoader.defineClass`, bypassing the filesystem entirely. OpenLDK has to parse those raw class bytes at runtime, generate CLOS class definitions on the fly, and slot them into the classloader hierarchy.

**Exception-based control flow.** Kawa implements `call/cc` (call-with-current-continuation) by throwing and catching exceptions. Invoking a continuation throws a `CalledContinuation` exception that unwinds the stack to the matching `callcc` handler. OpenLDK maps Java exceptions to Common Lisp conditions, and getting the control flow graph right at exception handler boundaries turned out to be one of the trickier problems to solve.

**A custom numeric tower.** Kawa implements the full Scheme numeric tower with its own class hierarchy under `gnu.math`: arbitrary-precision integers, exact rationals, complex numbers, even quantities with dimensional units. This exercises deep class hierarchies, virtual dispatch across many levels, and `sun.misc.Unsafe` for low-level field access on the numeric objects.

**Heavy use of reflection.** Kawa's compiler uses `java.lang.reflect` to inspect classes, and its module loader uses reflection to initialize static fields during class setup. This exercises `getDeclaredFields`, `getDeclaredMethods`, `Class.forName`, and the interaction between reflection and OpenLDK's CLOS-based class model.

Each of these pushed OpenLDK in directions that simpler Java programs never would. Getting `(+ 2 3)` to work required fixing primitive type descriptors in reflection and `Unsafe` static field access with null base objects. Getting the interactive REPL working meant handling TTY detection and stdin through the native method bridge. Kawa has been a great integration test for OpenLDK.

### Limitations

This is a technology demonstration, not a production system. The value conversion layer handles basic types (integers, floats, strings, booleans, and lists) but not the full breadth of Java's type system. OpenLDK requires Java 8's `rt.jar` for bootstrap classes. And performance hasn't been a priority; there are multiple layers of translation between your Scheme expression and the native code that eventually runs it.

### The Stack

cl-kawa depends on:

- **[SBCL](https://www.sbcl.org/)**, the host Common Lisp implementation
- **[OpenLDK](https://github.com/atgreen/openldk)**, the Common Lisp JVM
- **[Kawa 3.1.1](https://www.gnu.org/software/kawa/)**, the Scheme-to-JVM compiler
- **Java 8 JDK**, for bootstrap classes

The code is MIT licensed and available at [github.com/atgreen/cl-kawa](https://github.com/atgreen/cl-kawa).

Discuss on [Hacker News](https://news.ycombinator.com/item?id=47107664) or [Lobsters](https://lobste.rs/s/pvfqcr/cl_kawa_turducken_programming_languages).
