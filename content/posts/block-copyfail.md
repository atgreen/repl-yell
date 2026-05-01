---
title: "Mitigating Copy Fail Without a Reboot"
date: 2026-05-01
tags: ["common-lisp", "ebpf", "whistler", "security"]
summary: "A BPF LSM program that blocks CVE-2026-31431 at runtime."
---

[CVE-2026-31431](https://copy.fail/) ("Copy Fail") is a privilege
escalation in the Linux kernel's `authencesn` cryptographic template.
An unprivileged user can trigger it through a single `socket_bind`
call on an AF_ALG socket.

For systems where the vulnerable kernel module is built into the kernel,
the typical mitigation advice is to blacklist the affected kernel
module and reboot.  This works well, but rebooting can be very
disruptive in many environments.  It's often better if we can find a
temporary reboot-free mitigation until proper fixes are available and
can be rolled out in a standard patch window.

I wrote [block-copyfail](https://github.com/atgreen/block-copyfail) as
a proof-of-concept to fix this.  It's a BPF LSM program, written in
Common Lisp with [Whistler](https://github.com/atgreen/whistler), that
hooks `socket_bind` and blocks the exploit at runtime. Just run it,
and you're protected.

And this is a surgical mitigation. On every socket open, if the socket
family `AF_ALG` and the algorithm name `authencesn`, return
`-EPERM`. Everything else passes through untouched (dm-crypt, openssl
afalg, etc).  The only thing that stops working is the one algorithm
template that gets you rooted.

It logs every blocked attempt with PID, process name, and timestamp,
so you can see exactly who's trying to hit the vulnerability. Ctrl-C
detaches it cleanly.

Check it out on [GitHub](https://github.com/atgreen/block-copyfail).
