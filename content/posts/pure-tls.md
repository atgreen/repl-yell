---
title: "Building a TLS 1.3 Implementation in Pure Common Lisp"
date: 2026-01-03
tags: ["common-lisp", "tls", "cryptography", "security", "pure-lisp", "openssl", "ai", "llm"]
summary: "I built pure-tls, a TLS 1.3 implementation in Common Lisp to test the limits of AI code generation for a niche language."
---

Happy New Year!

For the past 20+ years, every Common Lisp application that needs HTTPS has traditionally had one option: [cl+ssl](https://github.com/cl-plus-ssl/cl-plus-ssl), a binding to OpenSSL. This works, but it means dealing with library versioning problems and platform-specific installation headaches.

I felt this pain personally for [ocicl](https://github.com/ocicl/ocicl), the Common Lisp package manager I maintain. On Windows, users had to source their own OpenSSL installation - and the options that exist rarely integrate with the Windows certificate trust store, leading to the decision to disable certificate validation for ocicl on Windows.

I knew TLS could be implemented in pure Common Lisp. [Ironclad](https://github.com/sharplispers/ironclad) already provides all the cryptographic primitives. The question was whether modern AI assistants could help write it in the short amount of time I was willing to invest, and for Common Lisp, an undeservedly niche language with far less training data than Python or JavaScript.

The result is [pure-tls](https://github.com/atgreen/pure-tls), a TLS 1.3 implementation with no C dependencies. It's new code, so treat it as experimental until it has significant production use and external review.

### Why TLS 1.3?

TLS 1.3 ([RFC 8446](https://tools.ietf.org/html/rfc8446)) is a significant simplification over TLS 1.2. The protocol designers removed legacy cruft, eliminated insecure cipher suites, and reduced the handshake from two round-trips to one. For an implementer, this is a gift: fewer code paths, fewer state machine transitions, and a cleaner specification.

The supported cipher suites are modern and well-understood:

- ChaCha20-Poly1305 (preferred for pure software implementations)
- AES-256-GCM
- AES-128-GCM

And the key exchange options are limited to elliptic curves:

- [X25519](https://en.wikipedia.org/wiki/Curve25519) (Curve25519)
- secp256r1 (P-256)

No RSA key exchange, no CBC mode, no MD5 or SHA-1. The attack surface is dramatically smaller than TLS 1.2.

TLS 1.3 adoption has been rapid - [around 70% of websites](https://sslinsights.com/ssl-certificates-statistics/) now support it, and that number continues to grow. Implementing only TLS 1.3 means pure-tls can connect to most of the web while keeping the codebase minimal.  Importantly, the github container registry that ocicl uses for [OCI](https://opencontainers.org/) artifact storage supports TLS 1.3.

### The Cryptographic Foundation

The heavy lifting comes from [Ironclad](https://github.com/sharplispers/ironclad), a mature cryptographic library for Common Lisp. Ironclad provides the primitives: AES, ChaCha20, Poly1305, SHA-256/384, HMAC, and elliptic curve operations. My job was to orchestrate these into the TLS 1.3 key schedule and record layer.

The TLS 1.3 key schedule is a cascade of [HKDF](https://tools.ietf.org/html/rfc5869) (HMAC-based Key Derivation Function) operations. Starting from the shared secret produced by the key exchange, you derive traffic secrets, then keys and IVs for each direction of communication. The RFC provides test vectors, so I could verify each step:

```lisp
;; RFC 8448 test vector verification
(test rfc8448-handshake-secret
  "Verify handshake secret derivation"
  (let ((shared-secret (hex-to-bytes "8bd4054fb..."))
        (hello-hash (hex-to-bytes "da75ce114..."))
        (expected (hex-to-bytes "1dc826e93...")))
    (is (equalp (derive-handshake-secret shared-secret hello-hash)
                expected))))
```

### Gray Streams: Seamless Integration

A TLS library is useless if it doesn't integrate cleanly with existing I/O code. Common Lisp's [gray streams](https://cliki.net/Gray%20streams) provide the answer. By implementing the [failed](https://www.nhplace.com/kent/CL/Issues/stream-definition-by-user.html)-but-defacto standard stream protocol, pure-tls appears as an ordinary binary stream:

```lisp
(pure-tls:with-tls-client-stream (tls socket :hostname "example.com")
  (write-sequence request-bytes tls)
  (force-output tls)
  (read-sequence response-buffer tls))
```

Under the hood, `write-sequence` encrypts application data into TLS records, and `read-sequence` decrypts them. The TLS machinery is invisible to application code.

### The Certificate Problem

Certificate validation is where TLS implementations get complicated. You need to:

1. Parse [X.509](https://en.wikipedia.org/wiki/X.509) certificates (a maze of [ASN.1](https://en.wikipedia.org/wiki/ASN.1) encoding)
2. Build and verify certificate chains
3. Check expiration, revocation, and key usage
4. Match hostnames against Subject Alternative Names

The ASN.1 parsing was implemented from scratch, following the DER encoding rules. It should have been tedious, but the AI made this simple work.

The harder problem is trust anchors. Where do you get the root CA certificates? On Linux, I search the standard locations (`/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`). But on Windows and macOS, the right answer is to use the operating system's trust store.

### Native Trust Store Integration

On Windows, I use CFFI to call the CryptoAPI. The `CertGetCertificateChain` and `CertVerifyCertificateChainPolicy` functions can build a certificate chain from a leaf certificate and validate it against the Windows certificate store when configured with the right policy flags, checking hostname, expiration, revocation, and trust. This means ocicl can validate certificates on Windows instead of disabling verification.

```lisp
(cffi:defcfun ("CertGetCertificateChain" %cert-get-chain) :boolean
  (chain-engine :pointer)
  (cert-context :pointer)
  (time :pointer)
  (additional-store :pointer)
  (chain-para :pointer)
  (flags :uint32)
  (reserved :pointer)
  (chain-context :pointer))
```

On macOS, it's the Security.framework and modern trust evaluation APIs (e.g., `SecTrustEvaluateWithError`). Same principle: let the OS do what it's good at.

This native integration means pure-tls automatically respects enterprise PKI deployments, certificate pinning policies, and trust store updates. No bundled CA certificates required.

### Drop-in cl+ssl Replacement

Many Common Lisp applications use libraries like Drakma for HTTP, which depend on cl+ssl. To make adoption easy, pure-tls includes a compatibility layer:

```lisp
;; In your .asd file, before defsystem:
(eval-when (:compile-toplevel :load-toplevel :execute)
  (asdf:load-system :pure-tls/cl+ssl-compat)
  (asdf:register-immutable-system "cl+ssl"))

(asdf:defsystem "my-application"
  :depends-on (:drakma ...))  ; Uses pure-tls transparently
```

The `asdf:register-immutable-system` trick tells ASDF that "cl+ssl" is already loaded, so when Drakma requests it, ASDF doesn't try to load the real OpenSSL-based version. Your application becomes pure Common Lisp with zero C dependencies for TLS.

### Side-Channel Hardening

Writing cryptographic code in a high-level language requires care. Some concerns:

**Constant-time comparisons**: MAC verification uses Ironclad's constant-time comparison functions. An early-return on mismatch would leak timing information.

**Uniform error handling**: All decryption failures produce identical error conditions. An attacker shouldn't be able to distinguish "bad padding" from "bad MAC" - that's exactly the information [padding oracle attacks](https://en.wikipedia.org/wiki/Padding_oracle_attack) exploit.

**Cipher suite choice**: ChaCha20-Poly1305 uses only add-rotate-xor operations, which makes it easier to keep constant-time in software. AES-GCM in pure software uses table lookups, making it potentially vulnerable to cache-timing attacks. I recommend ChaCha20-Poly1305 for pure software implementations.

These are only a few of the side-channel considerations; timing in scalar multiplication, AEAD tag verification, and cache effects in software crypto are also worth ongoing scrutiny.

### AI-Assisted Development

This project was partly an experiment in AI-assisted coding. I made extensive use of Claude and OpenAI's Codex throughout the implementation. Common Lisp is a niche language - there's far less training data than Python or JavaScript - so I wanted to see how well current models could handle it.

The AI assistants performed excellently. TLS requires niche knowledge - cryptographic protocols, ASN.1 encoding, platform-specific APIs - that I had little time to become expert in. Without AI assistance, I likely wouldn't have attempted this project at all.

TLS is also inherently testable, which gave me confidence to trust AI-generated code. The RFC provides test vectors for key derivation. Certificate parsing can be validated against known certificates. The handshake either works or it doesn't - you can't half-connect to cloudflare.com. Every component could be verified independently, so AI mistakes surfaced quickly.

That said, the implementation would benefit from more rigorous testing - fuzz testing in particular. And offloading certificate validation to the OS on Windows and macOS eliminates an entire class of potential bugs in chain building and trust evaluation.

The main annoyance was parentheses. Lisp's syntax should be trivial for an AI, but occasionally they'd drop or add a paren, then stumble around for minutes trying to pair them up correctly. Watching an AI flail at something a Lisp-aware editor handles automatically was a reminder that these tools still have blind spots.

### The Result

pure-tls is a brand new implementation with no real-world battle testing yet. But ocicl provides the perfect low-consequence application for a new TLS library: it downloads packages over HTTPS, there's no sensitive data at risk, and you can validate your downloads with signatures archived in [Sigstore's](https://www.sigstore.dev/) transparency log. It's an ideal proving ground.

As of ocicl 2.14, it uses pure-tls on Linux, Windows, and macOS.

The test suite validates against RFC test vectors and live connections to major sites, but production hardening comes from production use. Feature coverage varies across TLS 1.3 implementations, so check the README for what is currently implemented. If you try pure-tls and find issues, please [open an issue](https://github.com/atgreen/pure-tls/issues).

Is it faster than OpenSSL? No. OpenSSL has decades of optimization and often uses AES-NI hardware instructions. But for many applications, the benefits outweigh the performance difference:

- **Portability**: Works everywhere SBCL runs, no library installation
- **Simplicity**: No FFI complexity, no library versioning issues
- **Auditability**: The entire TLS stack is readable Common Lisp
- **Security posture**: No C code greatly reduces the risk of buffer overflows

The code is MIT licensed and available at [github.com/atgreen/pure-tls](https://github.com/atgreen/pure-tls), and you can install it with `ocicl install pure-tls`.
