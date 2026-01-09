---
title: "Automatic TLS Certificates for Common Lisp with pure-tls/acme"
date: 2026-01-09
tags: ["common-lisp", "tls", "acme", "lets-encrypt", "hunchentoot", "pure-lisp", "security"]
summary: "pure-tls now includes an ACME client for automatic Let's Encrypt certificates, with seamless Hunchentoot integration."
---

Once in a while I deploy little internet-facing services. When deployed on my homelab OpenShift cluster, TLS termination is handled for me - I just deploy my app and it gets HTTPS automatically. But sometimes I want to run something on a cloud VM, and then I need to deal with certificates myself. There are solutions like [Caddy](https://caddyserver.com/) that handle this, but that's one more thing to install and configure. I wanted something I could just deploy and have work right out of the box.

My recent [pure-tls](/repl-yell/posts/pure-tls/) project gave me the foundation - a pure Common Lisp TLS 1.3 implementation. To complete the picture, I've added `pure-tls/acme` for automatic [Let's Encrypt](https://letsencrypt.org/) certificates and `pure-tls/acme+hunchentoot` to integrate it with the [Hunchentoot](https://edicl.github.io/hunchentoot/) web server. The result: a Common Lisp web server that obtains and renews its own TLS certificates automatically. And this is all you need to make it work:

```lisp
(asdf:load-system :pure-tls/acme+hunchentoot)

(defvar *acceptor*
  (pure-tls/acme:make-acme-acceptor
    "example.com"
    "admin@example.com"
    :production t))

(hunchentoot:define-easy-handler (hello :uri "/") ()
  (setf (hunchentoot:content-type*) "text/plain")
  "Hello from pure-tls!")

(hunchentoot:start *acceptor*)
```

### The ACME Protocol

ACME (Automatic Certificate Management Environment, [RFC 8555](https://tools.ietf.org/html/rfc8555)) is the protocol behind Let's Encrypt. The basic flow is:

1. Create an account with the ACME server
2. Request a certificate for your domain(s)
3. Prove you control those domains by completing a challenge
4. Submit a Certificate Signing Request
5. Download the issued certificate

The interesting part is step 3 - domain validation. ACME defines several challenge types:

- **HTTP-01**: Serve a specific file at `/.well-known/acme-challenge/` on port 80
- **DNS-01**: Create a specific TXT record in your domain's DNS
- **TLS-ALPN-01**: Present a special self-signed certificate during a TLS handshake on port 443

I've always used DNS-01 validation in the past, but even with good tooling you're still waiting for DNS propagation. pure-tls's ALPN support gives us the opportunity to do something much better: **TLS-ALPN-01** validation happens over the same port 443 connection your server already uses. No DNS automation, no extra ports, no HTTP redirects, no separate validation server.

### TLS-ALPN-01: In-Band Validation

The TLS-ALPN-01 challenge ([RFC 8737](https://tools.ietf.org/html/rfc8737)) works by having Let's Encrypt connect to your server using TLS with a special ALPN protocol identifier: `acme-tls/1`. When your server sees this ALPN, it presents a validation certificate instead of your normal one.

This validation certificate is self-signed and contains a special extension (`1.3.6.1.5.5.7.1.31`, the acmeIdentifier OID) with a SHA-256 hash proving you control the account key and received the challenge token. Let's Encrypt verifies this hash, and domain validation succeeds.

The clever part: this all happens during the TLS handshake itself. Your web server can start immediately with a placeholder certificate, then switch to a real certificate once validation completes - typically within seconds. On first boot, visitors who arrive before issuance completes will see a certificate warning, but this window is brief.

### What Happens on Startup

On startup, the server first checks if it already has valid certificates. If so, it loads them and starts serving immediately. If not, it:

1. Generates a placeholder self-signed certificate
2. Starts listening on port 443 (you'll need root, `CAP_NET_BIND_SERVICE`, or a reverse proxy)
3. Contacts Let's Encrypt and completes TLS-ALPN-01 validation
4. Installs the real certificate
5. Starts a background thread to renew before expiration

The `:production t` flag uses Let's Encrypt's production servers. For testing, omit it or use `:production nil` to use the staging environment, which has much higher rate limits.

### Multi-Domain Certificates

If you're hosting multiple domains on the same server, pass a list:

```lisp
(pure-tls/acme:make-acme-acceptor
  '("example.com" "www.example.com" "api.example.com")
  "admin@example.com"
  :production t)
```

This requests a single certificate with all domains as Subject Alternative Names. Let's Encrypt will validate each domain before issuing.

### Certificate Storage

Certificates and account keys are stored persistently so they survive restarts. The default locations follow platform conventions:

- **Linux/Unix**: `$XDG_STATE_HOME/pure-tls/` or `~/.local/state/pure-tls/`
- **macOS**: `~/Library/Application Support/pure-tls/`
- **Windows**: `%LOCALAPPDATA%\pure-tls\`

The directory structure is:

```
pure-tls/
  account/
    account.key         # ECDSA P-256 account key (reused across domains)
  domains/
    example.com/
      privkey.pem       # Domain private key
      fullchain.pem     # Certificate chain
```

Private keys are saved with restrictive permissions (0600 on Unix). On restart, the server loads existing certificates if they're still valid.

### Automatic Renewal

The Hunchentoot acceptor spawns a background thread that checks certificate expiration daily. By default, it renews certificates 30 days before expiration (Let's Encrypt certificates are valid for 90 days). You can adjust this:

```lisp
(pure-tls/acme:make-acme-acceptor
  "example.com"
  "admin@example.com"
  :production t
  :renew-before-days 45)  ; Renew 45 days before expiration
```

Renewal happens in the background using the same TLS-ALPN-01 flow. Your server continues serving traffic with the old certificate until the new one is ready.

### You're Soaking In It

If you're reading this blog, your browser is probably already connecting to a pure-tls server. The analytics are powered by [Happening](https://github.com/atgreen/happening), a simple, privacy-focused web analytics platform written in Common Lisp and deployed on a free-tier EC2 instance.

Happening is a single-binary application. When you start it with an HTTPS URL:

```bash
ACME_EMAIL=admin@example.com ./happening -u https://analytics.example.com
```

It automatically obtains a Let's Encrypt certificate using pure-tls/acme+hunchentoot. No certbot, no cron jobs, no certificate files to manage. The tracking script uses `navigator.sendBeacon()` to record page views, and the server handles everything else - including certificate renewal - without any manual intervention.

This is the kind of deployment pure-tls enables: a self-contained application where HTTPS just works.

### Getting Started

To try it out:

```bash
ocicl install pure-tls
```

Or clone the repository:

```bash
git clone https://github.com/atgreen/pure-tls.git
```

The integration is in the `acme/` directory. Load `pure-tls/acme+hunchentoot` for the full Hunchentoot integration, or just `pure-tls/acme` if you want to use the ACME client directly with your own server.

As with pure-tls itself, this is new code. Test with Let's Encrypt staging first, and please [report any issues](https://github.com/atgreen/pure-tls/issues) you encounter. The goal is a complete, pure-Lisp HTTPS server stack - and automatic certificate management is a critical piece of that puzzle.
