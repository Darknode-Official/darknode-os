# DO-001 — Decision Memo: What Darknode OS Is

**Severity:** S0 (blocks everything downstream).
**Status:** DRAFT — **UNSIGNED**. No Track A or Track B build work (DO-010
through DO-016, and the Track A build spec) may begin until the owner signs this
memo. This memo was prepared by an engineering agent from reproduced source
evidence; the owner must make and sign the decision.
**Date:** 2026-10-04.

---

## 1. The problem (confirmed from source — `[C]`)

The name "Darknode OS" currently denotes two technically incompatible products:

1. **A from-scratch x86 kernel** — this repository
   (`Darknode-Official/darknode-os`). A real, bootable i686 kernel (~9,250 lines
   of C + NASM), verified this run to build with `gcc -m32` and boot to a GUI
   under QEMU. It is freestanding, has no TCP, no persistent filesystem, no real
   browser, and no security tooling. Its repository description claims a "GUI
   desktop, shell, filesystem, networking stack, and security tools" — the
   security-tools claim is unsupported by the source, and several desktop apps
   are static placeholders (see `CLAIMS-AUDIT-DO-001.md`). `[C]`

2. **A Linux-based security OS** — `Darknode-Official/darknode-app` ships a QEMU
   builder that produces a "Darknode OS" image on a Debian/Ubuntu/Kali base,
   inheriting a real kernel, real hardware support, a real browser, a real
   userland, and the existing security-tool ecosystem. `[C]`

A third fact sharpens the conflict: a separate working checkout of a repo also
named `darknode-os` (remote `Darknode-Official/darknode-os`) contains **both** the
from-scratch kernel **and** a Debian/Ubuntu QEMU builder in one tree. The two
incompatible products are already commingled under one name in at least one
place. `[C]`

These cannot both be "Darknode OS" without the description being false for one
of them. A from-scratch kernel at this stage **cannot** host Chromium/Firefox or
run third-party software; a Linux distribution is **not** "built from scratch in
C and Assembly." The current marketing (e.g. the kernel's fake browser paints
"Security Operating System / the platform for ethical hacking" plus the Linux
platform's line/tool counts) conflates the two.

## 2. The options

- **Track A — Research kernel.** `darknode-os` stays a from-scratch OS-dev
  project, marketed honestly as exactly that: impressive, educational, not a
  near-term daily driver.
- **Track B — Linux-based security OS [RECOMMENDED].** The shipping "Darknode
  OS" is a Linux distribution with a custom compositor, shell, theme, and
  agentic security stack. It inherits real hardware support, a real browser, a
  real userland, and the existing security-tool ecosystem on day one. Effort is
  spent on the differentiated part — the agent. This is the model Kali, Parrot,
  and Qubes use, and the model the existing `darknode-app` already uses.
- **Track C — Both, named differently.** The kernel keeps a research name; the
  shipping product takes "Darknode OS"; neither claims to be the other anywhere.

## 3. Recommendation: **Track B**

The shipping product that end users install and use for security engagements
should be a **Linux-based distribution** (Track B). Reasoning:

1. **The hard requirements of the shipping product are impossible on the
   from-scratch kernel in any realistic timeframe.** DO-012 requires a real
   browser; DO-011 requires GPU-accelerated Wayland compositing, fractional
   scaling, multi-monitor, HiDPI; DO-016 requires broad real-hardware support
   (GPU, Wi-Fi with monitor mode + injection, Bluetooth, suspend/resume). Each
   of these is multiple person-years of from-scratch driver and engine work. A
   Linux base provides all of them on day one.
2. **The differentiator is the agent, not the kernel.** The product's reason to
   exist is agentic security work with kernel-enforced authorization scope
   (DO-013/DO-014). That value is independent of whether the base kernel was
   written from scratch, and it is where effort should go.
3. **It matches reality already.** `darknode-app` already builds Darknode OS on
   a Debian/Ubuntu/Kali base. Track B formalizes what is already shipping rather
   than inventing a second path.
4. **Precedent.** Kali, Parrot, and Qubes are respected security OSes; none
   wrote their own kernel. Writing one is not what makes a security OS credible.

**Practical note on the kernel (the Track C overlap).** Recommending Track B for
the *shipping product* does not mean deleting the from-scratch kernel. The
kernel is real and genuinely boots; it has standalone value as a research /
education artifact. To satisfy Track B's rule that no surface describes Darknode
OS in two incompatible ways, the kernel must be **retained under a distinct
research name** (for example "Darknode Microkernel" or a research codename) and
must stop carrying the shipping product's branding and statistics. In effect the
recommendation is **Track B for the product, with the kernel preserved and
renamed as a research project** — which is Track C's separation applied to the
assets that already exist. If the owner prefers the kernel to *keep* the
`darknode-os` name, the correct choice is an explicit **Track C** instead, and
the shipping distribution must take a different name.

## 4. What is given up by choosing Track B

- The marketing claim "an OS built entirely from scratch in C and Assembly" no
  longer describes the shipping product. It describes the research kernel only.
- Some theoretical trust/minimalism arguments for a from-scratch TCB are
  forfeited for the shipping product (mitigated by MAC, sandboxing, and verified
  boot on the Linux base — DO-014).
- The from-scratch kernel stops being the headline of the product and becomes a
  separately-positioned project; its ongoing investment competes for time with
  the agent work.

## 5. Conditions that would reverse this recommendation

Reconsider Track A (from-scratch kernel as the shipping product) only if **all**
of the following become true:

1. The product's goals change such that a real browser, broad hardware support,
   and third-party software compatibility are explicitly dropped as
   requirements.
2. The from-scratch kernel demonstrably runs a real browser engine and the
   target hardware set, reproduced by a reviewer booting the image.
3. There is sustained engineering capacity to maintain drivers for real hardware
   from scratch.

Absent all three, Track B stands.

## 6. Decision and sign-off

| Field | Value |
|---|---|
| Recommended track | **B** (with the from-scratch kernel preserved under a research name; equivalently Track C if the kernel keeps the `darknode-os` name) |
| Owner's chosen track | _______________ |
| What happens to this kernel repo | _______________ |
| Shipping product name | _______________ |
| Signed (owner) | _______________ |
| Date | _______________ |

**Until this memo is signed, DO-010..DO-016 (Track B build) and the Track A
build spec are BLOCKED and must not begin.** The hygiene actions in DO-001 that
apply regardless of track (honest README, de-committing build binaries,
correcting the repo description) are *not* blocked and have been done on the
`do-001-ground-truth` branch.

## 7. Clarifications the owner must resolve

1. **Repo/path identity.** The engineering brief names
   `/home/manav/projects/darknode-os` as this repo, but that local path is a
   clone of `Darknode-Official/darknode-os` (a superset containing both the kernel
   and a Debian builder), not `Darknode-Official/darknode-os`. This work was done
   in a fresh clone of the official repo at
   `/home/manav/projects/darknode-os-official`. The owner should confirm which
   repository is canonical and reconcile the `Darknode-Official` checkout.
2. **Kernel's name and fate** under the chosen track (fields above).
3. **Where prebuilt images live.** Build binaries have been de-committed locally
   (DO-001); the owner must publish them as release artifacts and push the
   branch (the agent is forbidden from pushing or creating releases).
