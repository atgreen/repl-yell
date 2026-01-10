---
title: "A Primer on Post-Quantum Cryptography in TLS"
date: 2026-01-10
tags: ["pure-tls", "cryptography", "security", "common-lisp", "post-quantum"]
summary: "An introduction to post-quantum cryptography, ML-KEM, and how hybrid key exchange protects TLS connections against future quantum computer attacks."
---

Quantum computers capable of breaking today's cryptography don't exist
yet. But they might, and when they do, recorded sessions using
classical key exchange could become readable. This isn't science fiction; it's the
"harvest now, decrypt later" threat that's driving the largest
cryptographic transition since the move to public-key cryptography in
the 1970s.

I recently added post-quantum support to
[pure-tls](https://github.com/atgreen/pure-tls), my Common Lisp TLS
1.3 implementation. This post explains what post-quantum cryptography
is, why it matters for TLS, and how hybrid key exchange works.

### The Quantum Threat

Today's TLS connections typically use elliptic curve Diffie-Hellman
(ECDH) for key exchange. The security of ECDH rests on the difficulty
of the *discrete logarithm problem*: given a point on an elliptic
curve, it's computationally infeasible to determine the scalar that
produced it.

In 1994, Peter Shor published an [algorithm that solves this
problem](https://en.wikipedia.org/wiki/Shor%27s_algorithm) efficiently
on a quantum computer. Shor's algorithm also breaks RSA
and other public-key systems we depend on. A sufficiently powerful
quantum computer running Shor's algorithm could:

- Decrypt any TLS session it has a recording of
- Forge signatures
- Impersonate servers

No one knows when these computers will become real. Some say it may
never happen, while others think it could be within ten years. What's
certain is that attackers are already saving encrypted data, hoping
that quantum computers will let them read it in the future. This
"harvest now, decrypt later" threat is why the [European Commission
recommends](https://digital-strategy.ec.europa.eu/en/library/recommendation-coordinated-implementation-roadmap-transition-post-quantum-cryptography)
that critical infrastructure transition to post-quantum cryptography
by 2030.
Sensitive information like government messages, medical records, and
financial data, all could be at risk.

### Lattice-Based Cryptography

Post-quantum cryptography (PQC) uses algorithms that are thought to be
safe from both classical and quantum computers. There are several
types of math problems that can be used to build PQC systems:

- **Lattice problems** (ML-KEM, ML-DSA)
- **Code-based problems** (Classic McEliece)
- **Hash-based signatures** (SPHINCS+)
- **Isogeny problems** (SIKE, now broken)

[NIST held a multi-year
competition](https://csrc.nist.gov/projects/post-quantum-cryptography)
to choose standard PQC algorithms, finishing in 2024. For key encapsulation, which TLS uses for key
exchange, they picked **ML-KEM** (Module-Lattice Key Encapsulation
Mechanism), now published as [FIPS
203](https://csrc.nist.gov/pubs/fips/203/final).

ML-KEM is based on the *Module Learning with Errors* (MLWE) problem. In "simple" terms, this means finding a secret
vector from a set of nearly correct linear equations over a polynomial
ring. Experts think this problem is hard for quantum computers
because:

1. No efficient quantum algorithm for lattice problems is known
2. Lattice problems have been studied for decades without fundamental breakthroughs
3. The best known attacks scale exponentially

### How ML-KEM Works

ML-KEM is a *key encapsulation mechanism* (KEM), not a key exchange,
and this difference is important:

**Key Exchange (Diffie-Hellman style):**
- Both parties contribute randomness
- Both perform similar operations
- Symmetric in structure

**Key Encapsulation:**
- One party generates a keypair (encapsulation key, decapsulation key)
- The other party encapsulates a random secret using the public encapsulation key
- The first party decapsulates to recover the shared secret
- Asymmetric in structure

Here's the ML-KEM-768 flow:

```
   Client                                    Server
   ------                                    ------

   (ek, dk) = KeyGen()

                   ek (1184 bytes)
                 ------------------>
                                         (ss, ct) = Encaps(ek)

                   ct (1088 bytes)
                 <------------------

   ss = Decaps(dk, ct)

   [Both parties now share ss (32 bytes)]
```

The math behind ML-KEM uses polynomial rings, the Number Theoretic
Transform (NTT), and special compression to keep key sizes reasonable.
ML-KEM-768 aims for **NIST security level 3**, which is similar to
AES-192 or SHA-384 in classical security, with the following sizes:

| Component | Size |
|-----------|------|
| Encapsulation key (public) | 1184 bytes |
| Decapsulation key (private) | 2400 bytes |
| Ciphertext | 1088 bytes |
| Shared secret | 32 bytes |

For comparison, X25519 uses 32-byte public keys and shared secrets.
Post-quantum security means using more bandwidth.

### Why Hybrid Key Exchange?

ML-KEM is considered secure, but lattice-based cryptography is still
new in practice. Sometimes, cryptographic assumptions fail in
unexpected ways; SIKE, for example, was [broken in
2022](https://eprint.iacr.org/2022/975.pdf) right before NIST planned
to standardize it.

Hybrid key exchange combines a classical algorithm (X25519) with a
post-quantum algorithm (ML-KEM-768). The shared secret is derived from
both:

```
shared_secret = ML-KEM-ss || X25519-ss
```

This setup gives an important security benefit: as long as one of the
two algorithms is secure, the connection stays safe. If ML-KEM is
broken, X25519 still protects the data. If quantum computers break
X25519, ML-KEM still keeps the connection secure.

The specific hybrid used in TLS is **X25519MLKEM768** (per the TLS
hybrid KEM drafts and IANA registry at the time of writing). The key
shares look like this:

```
Client Key Share (1216 bytes):
+---------------------------+------------------+
|  ML-KEM encapsulation key |  X25519 public   |
|       (1184 bytes)        |   (32 bytes)     |
+---------------------------+------------------+

Server Key Share (1120 bytes):
+---------------------------+------------------+
|    ML-KEM ciphertext      |  X25519 public   |
|      (1088 bytes)         |   (32 bytes)     |
+---------------------------+------------------+
```

### TLS 1.3 Integration

TLS 1.3 is designed so it's easy to add new key exchange methods. The
`supported_groups` extension shows which groups a client supports, and
`key_share` holds the actual key data.

Here's a simplified view of a post-quantum TLS handshake:

```
Client                                           Server
------                                           ------

ClientHello
  supported_groups: [X25519MLKEM768, ...]
  key_share: X25519MLKEM768 (1216 bytes)
                        -------------------->
                                              ServerHello
                                                key_share: X25519MLKEM768 (1120 bytes)
                                              {EncryptedExtensions}
                                              {Certificate}
                                              {CertificateVerify}
                                              {Finished}
                        <--------------------

{Finished}
                        -------------------->

            [Application Data protected by hybrid secret]
```

The server runs both key exchanges at the same time:

1. X25519 ECDH with the client's X25519 public key
2. ML-KEM encapsulation with the client's encapsulation key

The combined 64-byte secret is then used by TLS 1.3's key derivation
function (HKDF) to create the real encryption keys.

### Implementing ML-KEM in pure-tls

To add post-quantum support to pure-tls, I had to build ML-KEM from
the ground up in Common Lisp. The main algorithm in
[`ml-kem.lisp`](https://github.com/atgreen/pure-tls/blob/master/src/crypto/ml-kem.lisp)
is about 800 lines long and includes:

- **NTT (Number Theoretic Transform):** Fast polynomial multiplication
  in the ring Z_q[X]/(X^256 + 1). Uses precomputed powers of the
  primitive 512th root of unity (17 mod 3329).

- **Sampling:** Rejection sampling from SHAKE128 output for uniform
  polynomials, and centered binomial distribution sampling from
  SHAKE256 for noise.

- **Compression/Decompression:** Reducing coefficient precision to fit
  ciphertexts in fewer bytes while maintaining correctability.

- **K-PKE:** The underlying IND-CPA public key encryption scheme that
  ML-KEM wraps with a Fujisaki-Okamoto transform for IND-CCA2 security.

The hardest part was making sure all operations run in constant time.
Side-channel attacks are a real risk, so ML-KEM code must avoid any
data-dependent branches or memory access. Some key techniques are:

```lisp
;; Constant-time Barrett reduction
(defun ct-barrett-reduce (x)
  "Reduce x mod 3329 in constant time using Barrett reduction."
  (let* ((v 20158)  ; floor(2^26 / 3329)
         (t (ash (* x v) -26)))
    (ct-cond-sub-q (- x (* t +mlkem-q+)))))

;; Constant-time conditional subtraction
(defun ct-cond-sub-q (x)
  "Subtract 3329 if x >= 3329, constant-time."
  (let ((mask (ash (+ x (- (ash 1 16) +mlkem-q+)) -16)))
    (- x (logand mask +mlkem-q+))))
```

Barrett reduction skips division, which could reveal timing details,
and the conditional subtraction uses bit tricks instead of branching.

### Validation and Interoperability

Cryptographic code needs thorough testing. pure-tls checks ML-KEM
against NIST's official Known Answer Tests (KATs), making sure both
decapsulation works and that invalid ciphertexts don't reveal anything
about the private key.

To test in real-world conditions, I tried it with Chrome, which now
supports X25519MLKEM768. The handshake worked as expected:

```
$ curl --tlsv1.3 https://localhost:8443/
TLS 1.3 connection established
Cipher: TLS_AES_256_GCM_SHA384
Key Exchange: X25519MLKEM768
```

### What's Next?

Post-quantum TLS is already available in major browsers and will
likely become the default in the next few years. The next big step is
certificate signatures. ML-DSA, which matches ML-KEM for signatures,
is now standardized, but using it in certificate chains is tricky
because the signatures are bigger than traditional ones.

In pure-tls version 1.6.0, X25519MLKEM768 is turned on by default. If
both sides support post-quantum key exchange, they'll use it
automatically, but the system still works with servers that only use
classical algorithms.

Quantum computers may still be years off, but the shift to
post-quantum security is already underway. Each connection that uses
post-quantum key exchange is one more conversation safe from future
quantum computers.

### Resources

- [FIPS 203: ML-KEM Specification](https://csrc.nist.gov/pubs/fips/203/final)
- [NIST Post-Quantum Cryptography Project](https://csrc.nist.gov/projects/post-quantum-cryptography)
- [pure-tls on GitHub](https://github.com/atgreen/pure-tls)
- [Cloudflare: Post-Quantum Cryptography](https://blog.cloudflare.com/post-quantum-cryptography-for-the-internet)
- [EU Post-Quantum Cryptography Roadmap](https://digital-strategy.ec.europa.eu/en/library/recommendation-coordinated-implementation-roadmap-transition-post-quantum-cryptography)
