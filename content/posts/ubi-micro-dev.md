---
title: "From UBI to Distroless"
date: 2026-01-05
tags: ["containers", "kubernetes", "ubi", "red-hat", "distroless", "devops", "security"]
summary: "Pre-built, distroless container images based on Red Hat UBI with Java, Node.js, and Python runtimes ready to go."
---

Red Hat's [Universal Base Image (UBI)](https://www.redhat.com/en/blog/introducing-red-hat-universal-base-image) Micro is a minimal container base - around 25MB with no package manager. It still includes bash and coreutils, but it's lean enough to serve as a foundation for truly distroless images.

Two problems remain. First, installing a runtime is painful. Without `dnf` or `rpm`, you're left with multi-stage builds or manually extracting RPMs. Second, even UBI Micro carries baggage - coreutils includes low-severity unpatched vulnerabilities. If your goal is zero CVEs, those have to go.

Last summer I challenged myself to build UBI-based container images with zero CVEs, using nothing but Red Hat's trusted UBI package content. The result is [ubi-micro-dev](https://github.com/ubi-micro-dev/ubi-micro-dev) - pre-built images with popular runtimes, and nothing more. The main purpose of this project is to demonstrate how you can build your own purpose-built distroless images.

### What's Available

The project offers three runtime flavors - OpenJDK, Node.js, and Python - with multiple versions of each. For example:

- **ubi9-micro-dev-openjdk-21** - Java 21 (LTS)
- **ubi9-micro-dev-nodejs-22** - Node.js 22
- **ubi9-micro-dev-python-3.12** - Python 3.12

All images are hosted on GitHub Container Registry and rebuilt every 6 hours with [fresh security scans](https://ubi-micro-dev.github.io/ubi-micro-dev/).

### Usage

Here's a Spring Boot application deployment:

```dockerfile
FROM ghcr.io/ubi-micro-dev/ubi9-micro-dev-openjdk-21:latest
ADD spring-petclinic-3.4.0-SNAPSHOT.jar .
ENTRYPOINT java -jar spring-petclinic-3.4.0-SNAPSHOT.jar
```

That's it. No multi-stage builds, no RPM extraction gymnastics. Your `Containerfile` stays readable.

### Why Distroless?

#### The CVE Problem

CVE counts are exploding. In 2024, nearly 40,000 CVEs were published. In 2025, that number [jumped to 48,185](https://jerrygamblin.com/2026/01/01/2025-cve-data-review/) - a 21% year-over-year increase.

![CVEs by Year](https://raw.githubusercontent.com/jgamblin/2025CVEBlog/main/graphs/01_cves_by_year.png)
*Source: [Jerry Gamblin](https://jerrygamblin.com/2026/01/01/2025-cve-data-review/)*

CVE volume doesn't equal risk - what matters is exploitability and whether the vulnerable code is actually reachable in your environment. But for organizations with container scanning gates in their CI/CD pipelines, this nuance gets lost. Every CVE in your base image is a potential blocker. Security teams flag it, developers investigate it, meetings get scheduled to assess risk, exceptions get filed. Even if the CVE is in code your application never executes, you still pay the process tax.

The math is simple: fewer packages means fewer CVEs means faster deployments.

#### Your SBOM is Too Big

Most container images ship software you'll never execute.

When you install `java-21-openjdk-headless` on a standard UBI 9 image, you don't just get OpenJDK. You get a dependency tree that includes Lua, NSS, Python, cups-libs, avahi-libs, and dozens more. Each package exists because some RPM maintainer thought it might be useful for some user in some scenario. Your scenario probably isn't that scenario.

Common sources of dependency bloat:

- **Package management**: DNF, RPM, and their dependencies exist to install software - not run it
- **"Recommended" or "Suggested" packages**: DNF installs weak dependencies by default
- **Tools for optional features**: Print support libraries when you'll never print
- **Scriptlet dependencies**: Python exists so RPM post-install scripts can run - once

Each extra package is another entry in your SBOM, another set of CVE mappings for your scanner to check, another potential red flag in your security review.

#### The Numbers

Trivy scan results tell the story. All three images below include `java-21-openjdk-headless` and can run Spring Boot applications. These results are as of the date of publishing:

<div style="display: flex; justify-content: center; margin-bottom: 2em;">
<table style="border-collapse: collapse; text-align: center;">
  <tr>
    <th style="padding: 12px 20px; border: 1px solid #444; text-align: left;">Image</th>
    <th style="padding: 12px 20px; border: 1px solid #444;">Low</th>
    <th style="padding: 12px 20px; border: 1px solid #444;">Medium</th>
    <th style="padding: 12px 20px; border: 1px solid #444;">High</th>
    <th style="padding: 12px 20px; border: 1px solid #444;">Critical</th>
  </tr>
  <tr>
    <td style="padding: 10px 20px; border: 1px solid #444; text-align: left;">UBI 9</td>
    <td style="padding: 10px 20px; border: 1px solid #444;">192</td>
    <td style="padding: 10px 20px; border: 1px solid #444;">39</td>
    <td style="padding: 10px 20px; border: 1px solid #444;">1</td>
    <td style="padding: 10px 20px; border: 1px solid #444;">0</td>
  </tr>
  <tr>
    <td style="padding: 10px 20px; border: 1px solid #444; text-align: left;">UBI-Minimal 9</td>
    <td style="padding: 10px 20px; border: 1px solid #444;">47</td>
    <td style="padding: 10px 20px; border: 1px solid #444;">33</td>
    <td style="padding: 10px 20px; border: 1px solid #444;">1</td>
    <td style="padding: 10px 20px; border: 1px solid #444;">0</td>
  </tr>
  <tr>
    <td style="padding: 10px 20px; border: 1px solid #444; text-align: left;">ubi-micro-dev</td>
    <td style="padding: 10px 20px; border: 1px solid #444; background-color: #1a4a1a;">0</td>
    <td style="padding: 10px 20px; border: 1px solid #444; background-color: #1a4a1a;">0</td>
    <td style="padding: 10px 20px; border: 1px solid #444; background-color: #1a4a1a;">0</td>
    <td style="padding: 10px 20px; border: 1px solid #444; background-color: #1a4a1a;">0</td>
  </tr>
</table>
</div>

Standard UBI 9 with OpenJDK carries 232 vulnerabilities across severity levels. UBI-Minimal cuts that to 81. But ubi-micro-dev hits zero.

Zero CVEs means nothing to triage, nothing to justify, nothing blocking your pipeline.

### The Build System

The full project is [available on GitHub](https://github.com/ubi-micro-dev/ubi-micro-dev) for review. The core of it is `build-umd-image.sh` - a script that takes the root set of packages you want to install, follows dependencies, and prunes all the way down until you get exactly what you need and no more.

#### The Problem with Normal Package Installation

When you run `dnf install java-21-openjdk-headless` on a standard UBI image, you don't just get OpenJDK. You get everything DNF thinks you might need: NSS for cryptographic services, Python for scriptlets, Lua for who-knows-what, SQLite for package databases. A "minimal" Java installation balloons with transitive dependencies.

ubi-micro-dev takes another approach: install everything normally, then surgically remove vestigial packages that serve no runtime purpose. This isn't a new idea - Ubuntu's [chiseled containers](https://ubuntu.com/containers/chiseled) do the same thing with Debian packages.

#### The Disallow List

The build script maintains a list of packages that must never appear in the final image, regardless of what depends on them:

```bash
cat > disallow <<'EOF'
alsa-lib
chkconfig
copy-jdk-configs
coreutils
coreutils-single
cups-libs
gawk
info
lua
ncurses-base
nspr
nss
nss-softokn
nss-softokn-freebl
nss-sysinit
nss-util
p11-kit
platform-python
platform-python-setuptools
python3
python3-libs
python3-pip-wheel
python3-setuptools-wheel
sqlite-libs
EOF
```

Why these specific packages? Each one is a potential source of bloat:

- **NSS/NSPR**: Mozilla's crypto libraries. If your Java app uses TLS, it uses OpenJDK's built-in crypto, not NSS. But NSS pulls in SQLite, which pulls in more. Excluding NSS breaks this dependency chain.
- **Python**: The platform Python (`/usr/libexec/platform-python`) exists for DNF and system scripts. Your container won't run DNF, so Python is dead weight.
- **coreutils/coreutils-single**: Shell utilities. Application runtimes almost certainly don't need `ls`, `cat`, or `chmod`.
- **Lua**: Gets pulled in by various RPM scriptlets. Not needed at runtime.

These are all tradeoffs. You can modify the disallow list to meet your specific needs. For example, if you need FIPS 140-2 certification, you'd keep NSS - OpenJDK's SunPKCS11 provider delegates to NSS for validated cryptography on RHEL.

#### The Dependency Closure Algorithm

After installing the requested packages, the script computes the minimal set of packages actually needed:

```bash
touch old
while ! cmp -s keep old; do
  <keep xargs rpm -r "$rootfs" -q --requires | sort -Vu | cut -d' ' -f1 \
    | grep -v '^rpmlib(' \
    | xargs -d $'\n' rpm -r "$rootfs" -q --whatprovides \
    | grep -v '^no package provides' \
    | sed -r 's/^(.*)-.*-.*$/\1/' \
    | grep -vxF -f disallow \
    > new || true
  mv keep old
  cat old new > keep
  sort -u keep -o keep
done
```

This loop:

1. **Queries requirements**: For every package in `keep`, ask RPM what it requires
2. **Resolves providers**: For each requirement, find which installed package provides it
3. **Extracts package names**: Strip version-release suffixes to get base package names
4. **Filters disallowed**: Remove anything on the disallow list
5. **Merges results**: Add newly discovered dependencies to `keep`
6. **Repeats**: Keep going until the set stabilizes (no new packages discovered)

The key insight is step 4. When we discover that `java-21-openjdk-headless` requires something provided by `nss`, we simply don't add `nss` to the keep list. The dependency exists in RPM's database, but we're about to delete RPM's database anyway.

#### The Pruning Phase

Once we have the minimal `keep` set, everything else gets deleted:

```bash
rpm -r "$rootfs" -qa | sed -r 's/^(.*)-.*-.*$/\1/' | sort -u > all
grep -vxF -f keep all > remove

<remove xargs rpm -v -r "$rootfs" --erase --nodeps --allmatches
```

The `--nodeps` flag is critical. RPM would normally refuse to remove `nss` because `java-21-openjdk-headless` depends on it. We're telling RPM "I know what I'm doing, delete it anyway."

This works because RPM dependency metadata is conservative - it describes what *might* be needed, not what's actually required at runtime. The disallowed packages exist for optional features, or edge cases your application is never expected to hit.

#### The Multi-Stage Build

The Containerfiles tie this together with a standard multi-stage pattern:

```dockerfile
FROM registry.access.redhat.com/ubi9 AS ubi-micro-build
ADD build-umd-image.sh .
RUN UBI_VERSION=9 ./build-umd-image.sh java-21-openjdk-headless \
    glibc-langpack-en glibc-minimal-langpack

FROM scratch
COPY --from=ubi-micro-build /mnt/rootfs /
USER 1001
```

Stage one uses full UBI 9 (with DNF) to run the build script. The script constructs a minimal rootfs at `/mnt/rootfs`. Stage two starts from `scratch` - literally nothing - and copies only that rootfs. The final image contains no build tools, no DNF, no package cache.

The result is an image where you can account for every byte.

### Scanning and Transparency

The images are rebuilt regularly via scheduled GitHub Actions workflows. Each rebuild triggers vulnerability scanning, and the results are published so you can verify the security posture before deploying.

The reports include Trivy and Grype scan results, Red Hat's assessment of each CVE, and an opinion on whether the vulnerability is actually applicable. Unfortunately, scanners still flag software components for CVEs that don't actually affect the included code, so this extra assessment is necessary.

For example, [CVE-2025-64720](https://access.redhat.com/security/cve/CVE-2025-64720) is a libpng vulnerability that scanners flag against `java-21-openjdk-headless`. But the vulnerable code only exists in `libsplashscreen.so`, which is part of `java-*-openjdk` - not the headless package. Red Hat's CVE page makes this distinction, but users have to consult it manually. The ubi-micro-dev reports surface this information directly.

### Getting Started

Pull an image and use it:

```bash
podman pull ghcr.io/ubi-micro-dev/ubi9-micro-dev-openjdk-21:latest
```

Build your application image:

```dockerfile
FROM ghcr.io/ubi-micro-dev/ubi9-micro-dev-openjdk-21:latest
ADD myapp.jar .
ENTRYPOINT ["java", "-jar", "myapp.jar"]
```

Check the [scan reports](https://ubi-micro-dev.github.io/ubi-micro-dev/) before deploying. If you have questions, [file an issue](https://github.com/ubi-micro-dev/ubi-micro-dev/issues) on GitHub.

### Project Hummingbird

I started ubi-micro-dev as an experiment before learning about [Project Hummingbird](https://www.redhat.com/en/technologies/linux-platforms/enterprise-linux/hummingbird), Red Hat's official take on minimal container images. But the two are different enough in nature that I wanted to share this work anyway.

ubi-micro-dev uses RHEL packages from UBI repositories. Rather than mirroring RHEL, Hummingbird is designed to track the latest upstream sources, ensuring the fastest possible access to CVE fixes and updates.

If you need RHEL package compatibility, or you're curious about building your own distroless images from trusted UBI content, an approach like ubi-micro-dev might be worth exploring.

### Conclusion

Distroless containers shouldn't require a PhD in multi-stage builds. More than providing ready-to-use images, ubi-micro-dev demonstrates the path to building your own purpose-built, distroless images from trusted UBI package content.

The project is open source at [github.com/ubi-micro-dev/ubi-micro-dev](https://github.com/ubi-micro-dev/ubi-micro-dev).
