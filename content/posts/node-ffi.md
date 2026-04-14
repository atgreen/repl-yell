---
title: "libffi Lands in Node.js"
date: 2026-04-14
tags: ["libffi", "nodejs", "ffi"]
summary: "Node.js is shipping a built-in node:ffi module powered by libffi — a library I wrote 30 years ago."
---

Node.js is getting a built-in [`node:ffi`](https://github.com/nodejs/node/blob/main/doc/api/ffi.md) module. It lets you load shared libraries and call native functions directly from JavaScript, without any kind of wrappers or extra build step. I just heard that it landed in the upstream git repo about an hour ago, and should appear in Node 26.

Here's what it looks like:

```javascript
import { dlopen } from 'node:ffi';

const { functions } = dlopen(`libsqlite3.so`, {
  sqlite3_libversion: { parameters: [], result: 'string' },
});

console.log(functions.sqlite3_libversion());
```

That's it. Load a `.so`, declare the signature, call the function. Callbacks work too. You can pass JavaScript functions as native function pointers to your C code.

This is interesting to me, as I created
[libffi](https://sourceware.org/libffi/) 30 years ago last month. It
started as a way to build a generic calling convention bridge for the
GNU java compiler and runtime, but now it's bundled as a core language
feature in each of Python, Ruby, and (as of this module) Node.js,
along with a number of Java ports.

Here's a good walkthrough of the implementation:

{{< youtube c5S1zcQaJRw >}} 

Congratulations to the Node team for landing this!

