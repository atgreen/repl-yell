---
title: "OpenLDK: Bringing the Java World into Common Lisp"
date: 2026-10-25
tags: ["common-lisp", "java", "openldk", "jvm", "sbcl", "polyglot"]
summary: "Java classes become CLOS classes, seven JVM languages share one Lisp expression, and a saved Spring Boot application launches in 84 milliseconds."
---

Many years ago I worked on an alternate Java compiler and runtime
called gcj, the GNU Compiler for the Java Programming Language.
These days, my preferred programming language for most tasks is
Common Lisp, but I'm still interested in Java runtimes.

Back in late 2024, I wanted to use a Java library from SBCL, but
couldn't find an integration approach that suited the application.
This led me to explore the idea of building a simple JVM bytecode
processor in Lisp so I could execute the code directly in process.

I started before coding agents became useful for this kind of work,
and wrote the first working version by hand.

[OpenLDK](https://github.com/atgreen/openldk) is a JIT compiler and
runtime for Java, written in Common Lisp. It reads Java bytecode,
translates it into Lisp, and lets SBCL compile that Lisp into native
machine code. Java objects live in the same process and heap as the
Lisp application.

My goal is to make Java libraries feel at home in Common Lisp. For
general Java execution, HotSpot will be much faster.

In the last few days, however, OpenLDK has gotten much better.  I've
experimented with JVM-hosted languages and Spring Boot application
execution.

OpenLDK leads to some wonderfully strange possibilities. Java classes
become CLOS classes. Lisp can add methods around methods written in
Kotlin. A Clojure function can become something you pass to
`funcall`. Java's own compiler runs inside SBCL. And an initialized
Spring Boot application can be saved in a Lisp image and launched
again in about 84 milliseconds.

You can even connect Emacs and [SLY](https://joaotavora.github.io/sly/)
to a Spring Boot application running on OpenLDK, inspect its objects,
and monkey-patch its behavior while it keeps running.

Here is a tour of how it works, what it can do, and what that startup number actually means.

The compilation pipeline begins with ordinary class files. OpenLDK targets JDK 25 and uses OpenJDK's runtime libraries, reading them from the JDK's JMOD files or its `lib/modules` container.

When a class loads, OpenLDK creates its CLOS representation and installs stubs for its methods. Calling a method for the first time triggers compilation: bytecode becomes an intermediate representation, then Lisp, then native code through SBCL. The compiled definition replaces the stub.

That laziness matters. Loading a library does not mean compiling every method in it. Much of a large library may never be used by a particular application.

Consider a Java method:

```java
public static int add(int x, int y) {
    return x + y;
}
```

Stripped of the generated control-flow scaffolding and exception machinery, its translated body amounts to:

```lisp
(sb-c::mask-signed-field 32 (+ x y))
```

The signed-field operation preserves Java's 32-bit integer overflow behavior. With typed operands, SBCL can compile this to a machine addition. OpenLDK handles Java's semantics while drawing on the compiler SBCL already provides.

The translation also makes the program accessible to Lisp. Java fields become slots. Instance methods become methods of CLOS generic functions, identified by their JVM signatures so overloaded methods remain distinct. Static methods become ordinary Lisp functions. Java's exception hierarchy has a corresponding Lisp condition hierarchy.

Compiled methods are retained in a persistent code cache. Subsequent runs can load cached code when the classes it depends on remain unchanged, and cache entries are separated by OpenLDK build. Different images can reuse compiled JDK methods too.

The most interesting consequences appear when an application starts using these representations directly.

Foreign source can appear directly inside Lisp source. After starting the relevant languages and enabling OpenLDK's source syntax, a Kotlin expression looks like this:

```lisp
(let ((n 4))
  #@kt{ (1..$n).map { it * it } })
;; => (1 4 9 16)
```

`$n` passes the Lisp value into the Kotlin code as an object. Lexical variables, collections, and callbacks can cross the boundary without being serialized into source strings.

The same mechanism supports Scala, Groovy, Clojure, Kawa, JScheme, and ABCL. Each block keeps the language's own syntax. Clojure also has a `#[ ... ]` notation.

A Clojure function can come back as a callable foreign-function object. The surrounding Lisp invokes it with ordinary `funcall`, with argument and result conversion handled by the interop layer. The reverse direction works too: Lisp functions can become callbacks for the other languages.

You can also nest those source blocks. Here is the entire [`examples/one-form.lisp`](https://github.com/atgreen/openldk/blob/39f33c0/examples/one-form.lisp) file, verbatim. After the setup, one expression passes a value through all seven languages:

```lisp
;;;; One form, seven languages
;;;;
;;;; A single Lisp expression in which every JVM language OpenLDK runs does
;;;; one thing it is known for, written in its own syntax, and hands its
;;;; result to the next.  Read it from the inside out:
;;;;
;;;;   Clojure  takes ten primes from an infinite lazy sequence
;;;;            (2 3 5 7 11 13 17 19 23 29)
;;;;   Groovy   squares them with the spread-dot operator and sums: 2397
;;;;   Kawa     adds one with Java's Long.sum, by colon notation, and
;;;;            divides exactly by the first of 6, 5, 4, 3, 2 that doesn't
;;;;            divide it evenly, escaping the search with call/cc: 1199/3,
;;;;            returned as its numerator and denominator
;;;;   JScheme  sums each one's digits in a named let: (20 3)
;;;;   Scala    matches the pair against a list pattern with a guard: 60
;;;;   Kotlin   pours it into a local data class, copies it, and sweetens it
;;;;            with when and the Elvis operator: 61
;;;;   ABCL     writes it with FORMAT, in Roman numerals and in English
;;;;
;;;; Each block is source in its language, as the language writes it.
;;;; $#@lang{ ... } (or $#[ ... ] for Clojure) splices the value of the
;;;; block inside into the language around it, passed as an object.
;;;;
;;;;   ./sbcl-polyglot --load examples/one-form.lisp
;;;;
;;;; prints: LXI (sixty-one)

(require :asdf)
(push (uiop:getcwd) asdf:*central-registry*)
(asdf:load-system "openldk/polyglot")
(polyglot:startup :verbose nil)
(setf *readtable* (polyglot:enable-syntax (copy-readtable)))

(format t "~&~A~%"
  #@abcl{                                ; ABCL: FORMAT's Roman numerals and English
    (let ((n $#@kt{                      // Kotlin: a local data class, copy, when, ?:
               data class Cup(val tea: Int, val sugar: Int? = null)
               val cup = Cup($#@sc{     // Scala: a list pattern with a guard
                               $#@jscheme{   ; JScheme: digit sums in a named let
                                 (map (lambda (n)
                                        (let loop ((n n) (sum 0))
                                          (if (< n 1)
                                              sum
                                              (loop (quotient n 10) (+ sum (remainder n 10))))))
                                      $#@kawa{   ; Kawa: Java by colon notation; call/cc; exact division
                                        (let* ((m (java.lang.Long:sum
                                                   $#@gv{   // Groovy: the spread-dot operator
                                                     $#[ (->> (iterate inc 2)   ; Clojure: lazy and infinite
                                                              (filter (fn [n] (not-any? #(zero? (rem n %))
                                                                                        (range 2 n))))
                                                              (take 10)) ]*.power(2).sum()
                                                   }
                                                   1))
                                               (d (call-with-current-continuation
                                                    (lambda (found)
                                                      (for-each (lambda (d)
                                                                  (if (not (= 0 (remainder m d))) (found d)))
                                                                '(6 5 4 3 2))
                                                      1)))
                                               (r (/ m d)))
                                          (list (numerator r) (denominator r)))
                                      })
                               } match {
                                 case List(a: Int, b: Int) if a > b => a * b
                                 case List(a: Int, b: Int) => a + b
                               }
                             }).copy(sugar = null)
               when { cup.tea > 40 -> cup.tea + (cup.sugar ?: 1); else -> cup.tea }
             }))
      (format nil "~@R (~R)" n n))
  })
```

The result:

```text
LXI (sixty-one)
```

The `$` before a nested block passes its result to the enclosing language. The expression crosses seven language implementations, using their own syntax and libraries, inside one Lisp process. I think that's extremely cool.

CLOS integration goes further. Lisp can add a method that runs before Java's `ArrayList.clear()`:

```lisp
(openldk.interop:define-java-method :before
    ("java.util.ArrayList" "clear()V") (list)
  (format t "~&clearing ~D elements~%"
          (openldk.interop:java-call "size()I" list)))
```

The signature's `V` means the method returns Java's `void`; `size()I` returns an integer. This method prints the number of elements before the original implementation clears the list.

Calls from compiled Java, Kotlin, Scala, or Groovy code go through it too.

Java methods participate in CLOS method combination. Lisp can supply `:before`, `:after`, and `:around` behavior at the point where the application needs it.

Lisp can also define a class that Java code sees as an ordinary Java class. OpenLDK's `define-java-class` generates a class file and installs it through the application class loader, while its method implementations are Lisp code.

These classes can implement Java interfaces. Their methods can consult Lisp state and call other language components, while Java reflection sees an ordinary Java class.

Error handling participates in the same integration. A `NumberFormatException` escaping a foreign call becomes a typed Lisp condition, `foreign-number-format-error`, which is also a `parse-error`. Other mappings cover arithmetic, file, type, and storage errors.

The foreign-call interface offers `use-value` and `retry` restarts. Lisp handlers can supply a replacement value or retry a failed foreign call.

A Lisp error raised inside a callback can travel in the other direction as an appropriate Java exception. If it escapes back to Lisp, the original Lisp condition is recovered. Each side can use its own error-handling facilities.

Collections have similar accommodations. A Java list can be exposed as a Lisp sequence, converting elements as they are accessed. A Lisp sequence or hash table can be presented to Java through a backed list or map view.

The build system joins in as well. ASDF systems can contain Maven JAR components and source components for the supported languages. Kotlin, Scala, and Groovy files compile during ASDF's compilation operation; their classes become available to later components. Maven components place libraries on the shared classpath. Dependencies must currently be listed explicitly.

Inline Kotlin and Scala blocks can also compile with the surrounding Lisp file when their splices have static types and the language compiler is running. Their class bytes are stored in the FASL. Loading that FASL does not require recompiling those blocks. The [interop documentation](https://github.com/atgreen/openldk/blob/39f33c0/docs/INTEROP.md) covers these interfaces and the language-specific conversions.

These features sit on top of some substantial programs already running.

Java's `javac` compiler works on OpenLDK, and the build produces a warmed `javacl` executable. Clojure runs with protocols, records, atoms, and futures. Kawa and JScheme run Scheme. Scala's compiler and REPL work, Kotlin compiles source into executable class files, and Groovy runs dynamic scripts and statically compiled classes.

ABCL adds a particularly pleasing loop: Common Lisp compiled into Java bytecode, translated back into Common Lisp, then compiled by SBCL. Its REPL, compiler, CLOS, conditions, and Java interop run in that environment.

There are also Spring Boot and Quarkus applications. Spring PetClinic runs with embedded Tomcat, Hibernate, and H2. That is separate from the small command-line application used for the startup measurements below.

All that compilation has a cost. Starting a large language implementation cold can take minutes, because OpenLDK is compiling the implementation itself as it runs. Saving executable images makes a substantial difference.

A warm image runs an application during the build, exercises selected paths, and saves the compiled code. Paths the warmup never exercised still compile on first use.

Application state is a separate choice. By default, `dump-warm-image` resets application-class static state so initialization can happen again. With `:reset-classes nil`, it preserves the state left by the warmups. An application designed to reuse that state can begin with much of its initialization already complete.

That is how the Spring Boot snapshot works.

The benchmark application uses auto-configuration, configuration properties bound into a record, dependency injection, and an auto-configured Jackson mapper. A `CommandLineRunner` produces JSON and reads a value back. Ordinary startup also prints Spring's banner and logs through Logback.

Its snapshot entry point keeps the initialized context:

```java
public class Snapshot {
    static ConfigurableApplicationContext ctx;

    public static void main(String[] args) throws Exception {
        if (ctx == null) {
            ctx = SpringApplication.run(BootApp.class, args);
        } else {
            for (CommandLineRunner runner :
                    ctx.getBeansOfType(CommandLineRunner.class).values()) {
                runner.run(args);
            }
        }
    }
}
```

With the Spring imports supplied, this is the benchmark's entry point. The first call starts Spring. Later calls reuse the context and execute its runners.

After loading OpenLDK into SBCL and setting `CLASSPATH` to the application classes and each required JAR, the image can be built with:

```lisp
(openldk::dump-warm-image
 "boot-snapshot" "demo.boot.Snapshot"
 :classpath (uiop:getenv "CLASSPATH")
 :warmups (list nil nil)
 :reset-classes nil)
```

The first warmup initializes Spring; the second exercises the reuse path. Saving the image preserves that initialized object graph.

This is similar in spirit to [CRaC (Coordinated Restore at Checkpoint)](https://crac.org/about/about-crac/): initialize the application once, save its state, and reuse that work on subsequent launches. CRaC restores a checkpointed JVM process. OpenLDK uses SBCL's image-saving machinery and calls `main` again with the Spring context already present. It does not implement the CRaC API or preserve running threads, so thread pools and other process-local resources need attention when designing an application for this approach.

Here are the results from my recent run:

- Host: Intel(R) Core(TM) Ultra 7 255H, 16 CPUs
- JDK: openjdk version "25.0.4.1" 2026-08-18
- GraalVM: native-image 25.0.4.1.1 2026-08-18
- OpenLDK: `39f33c0`
- Load average before: `3.75 5.29 3.83`
- Twenty timed runs per variant

| Variant | Median | Min | Max | Peak RSS |
|---|---|---|---|---|
| HotSpot, fat jar | 2138.3 ms | 2078.9 ms | 2219.0 ms | 266 MB |
| HotSpot, extracted jar | 1791.3 ms | 1755.0 ms | 1880.1 ms | 225 MB |
| HotSpot + Spring AOT | 1481.4 ms | 1420.5 ms | 1575.8 ms | 194 MB |
| HotSpot + JDK AOT cache | 758.8 ms | 713.4 ms | 828.8 ms | 181 MB |
| HotSpot + Spring AOT + AOT cache | 595.1 ms | 568.5 ms | 625.6 ms | 157 MB |
| **GraalVM native image** | **53.5 ms** | **50.9 ms** | **63.1 ms** | **41 MB** |
| OpenLDK warm image | 1261.7 ms | 1237.8 ms | 1283.2 ms | 450 MB |
| **OpenLDK snapshot image** | **83.9 ms** | **77.5 ms** | **88.6 ms** | **175 MB** |

These timings cover process launch through exit for the small command-line application; peak RSS is measured separately. The [benchmark script](https://github.com/atgreen/openldk/tree/39f33c0/bench/spring-boot) checks that the application output matches across variants. This measures startup and a short task, not steady-state throughput or web-server readiness.

GraalVM is faster and smaller here: **53.5 ms and 41 MB**, against OpenLDK's **83.9 ms and 175 MB**. GraalVM still runs Spring initialization at launch; OpenLDK reuses the saved context.

Standard GraalVM Native Image makes a [closed-world assumption](https://www.graalvm.org/jdk25/reference-manual/native-image/metadata/#defining-classes-at-run-time): all classes must be known at build time, with dynamic accesses covered by analysis or metadata. **OpenLDK keeps its class loader and JIT compiler**, so new classes and previously unused methods can still compile after launch. Those paths pay their compilation cost when first used.

Saved images also need their classpath files and JDK available at runtime. The [image documentation](https://github.com/atgreen/openldk/blob/39f33c0/docs/IMAGES.md) covers deployment constraints.

OpenLDK still has compatibility gaps, including unimplemented bytecode verification. Development targets SBCL on Linux.

To try the polyglot examples, follow the [build requirements](https://github.com/atgreen/openldk#requirements), then run these commands from the OpenLDK checkout:

```sh
ocicl install
make sbcl-polyglot
./sbcl-polyglot --load examples/one-form.lisp
```

Seven languages later: `LXI (sixty-one)`.
