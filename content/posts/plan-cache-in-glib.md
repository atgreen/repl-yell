---
title: "The libffi plan cache is coming to GLib"
date: 2026-08-01
tags: ["libffi", "ffi", "performance", "glib"]
summary: "Christian Hergert wired the new ffi_call_plan API into GLib and made GClosure invocation up to 25 percent faster."
---

A month ago I [wrote about a new libffi API]({{< ref "posts/libffi-plan-cache" >}}) that lets a caller build the argument-placement plan once and reuse it, instead of re-deriving it on every `ffi_call`.

I'm happy to see Christian Hergert [put it to work in GLib](https://blogs.gnome.org/chergert/2026/07/24/ffi_call_plan-caching-for-glib/). He caches the plan for GClosure invocation and measures it up to 25 percent faster in the best case, and still around 10 percent when conditions are less than ideal.

That's exactly what I hoped would happen: build the thing once, and let it save the work on every call after.
