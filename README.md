# Darknode OS

A self-provisioning security workstation — a Kali/BlackArch alternative that wraps a
neutral cloud base (Debian, Ubuntu, or Kali) in the **Darknode** identity: a branded
XFCE desktop, a curated offensive/defensive toolset, a local-AI layer, and a built-in
honeypot. You pick the base OS at build time; everything else is installed on first
boot by cloud-init.

## Architecture

```
   Base cloud image  (Debian 12 · Ubuntu 24.04/22.04 · Kali)
          |
          |  build.sh   (chooses the base, builds a cloud-init NoCloud seed)
          v
   Darknode OS disk  +  seed.iso
          |
          |  first boot -> cloud-init runs the provisioner
          v
   +----------------------------------------------------------+
   |  Desktop      XFCE · dark theme · Whisker menu · Chrome   |
   |  Toolkit      recon · web · passwords · wireless · forensics
   |  AI layer     Ollama + darknode-ai -> recon / triage / engage / loot
   |  Defense      darknode-lockout · darknode-scope · darknode-anon
   |  Honeypot     bundled deception service                   |
   +----------------------------------------------------------+
          |
          v
   Run it:  launch.sh (QEMU/KVM)   or   export-vbox.sh (VirtualBox)
```

The base image never carries the identity — `build.sh` stamps the chosen family into
the cloud-init seed, and the provisioner adapts per distro (kernel package, repos)
before installing the desktop, tools, and AI layer.

## Project Structure

```
darknode-os/
├── build.sh              # OS picker + base-image fetch + cloud-init seed builder
├── launch.sh             # boot the VM under QEMU/KVM
├── export-vbox.sh        # import the disk into VirtualBox (virtio-scsi, auto-resize)
├── darknode-os.conf      # remembered base-OS choice (the "setting")
├── core/                 # from-source core: mainline Linux 6.12 kernel + BusyBox + Darknode init (core/README.md)
├── cloud-init/
│   ├── user-data         # the provisioner: desktop, tools, AI, branding, hardening
│   └── meta-data         # NoCloud instance metadata
└── toolkit/              # everything installed into the OS
    ├── darknode-tools    # discover/search/launch the custom toolkit (start here)
    ├── darknode-ai       # shared AI engine (local Ollama + cloud), used by the tools
    ├── darknode-scope    # engagement authorization guard (in-scope enforcement)
    ├── darknode-recon    # AI-orchestrated recon -> prioritized findings
    ├── darknode-loot     # AI findings register from raw scan output
    ├── darknode-triage / darknode-engage   # AI triage + report
    ├── darknode-lockout  # emergency defensive lockdown
    ├── darknode-anon / darknode-stealth / darknode-macspoof   # anonymity
    ├── darknode-desktop-setup / gen-darknode-menu             # first-login desktop
    └── assets            # wallpaper, icons, Chrome/desktop assets
```

## Choosing the base OS

```
./build.sh              # interactive picker
./build.sh ubuntu       # or name it
DARKNODE_BASE=debian ./build.sh
```

Debian and Ubuntu are fully wired; Kali is experimental. Windows/Fedora/Arch aren't
built by this pipeline (different provisioners) and the picker says so.

## Editions

Pick how much ships — every edition self-provisions on first boot:

| Edition | Disk | UI | What's included |
|---|---|---|---|
| **netinstall** | ~12 GB | none (terminal only) | Core CLI security stack + Nexus AI agent + local models. Boots to a console with tty1 autologin. Smallest & fastest. |
| **slim** | ~20 GB | XFCE desktop | Full branded desktop + Nexus + the CLI toolset, **minus** Metasploit, SecLists, Exploit-DB, Docker and the cockpit app. |
| **full** | ~30 GB | XFCE desktop | Everything: desktop, cockpit app, Metasploit, SecLists, Exploit-DB, Docker, autonomous AI recon, honeypot — 177 tools. |

```
./build.sh debian netinstall     # base + edition (default edition is full)
./build.sh debian slim
./build.sh debian full
DARKNODE_BASE=debian DARKNODE_EDITION=slim ./build.sh
```

Editions build to separate files (`darknode-os-<edition>.qcow2` / `seed-<edition>.iso`) so they don't clobber each other. Pass the edition to the launchers too: `./launch.sh slim`, `./export-vbox.sh netinstall`.

**core** is different: not a cloud-image edition but the from-source kernel core — the mainline **Linux 6.12** kernel compiled from `torvalds/linux` source, a static BusyBox userland and a Darknode `init`. ~15 MB total, no base image, no cloud-init, boots to a Darknode shell in about 3 s under QEMU. It builds and boots on its own: `cd core && ./build.sh && ./run.sh -nographic` (`./run.sh -selftest` boots headless and asserts the system reached userspace). See [`core/README.md`](core/README.md).

## Installation

```
git clone https://github.com/Darknode-Official/darknode-os-distro
cd darknode-os-distro
./build.sh debian full        # pick a base + edition; builds the disk + cloud-init seed
./launch.sh full              # QEMU/KVM   (or ./export-vbox.sh full for VirtualBox)
```

First boot self-provisions (netinstall ~5–8 min, full ~15–20 min). Login: `darknode` / `darknode`.

## Status

Active. Debian/Ubuntu bases fully supported; VirtualBox Guest Additions, Chrome,
the AI toolkit, and the defensive tools are provisioned on first boot.

## Security

Darknode OS ships offensive tooling for **authorized** testing only. Use
`darknode-scope` to declare in-scope targets — the AI tools refuse anything outside
it. Do not distribute images with live credentials baked in.

## License

See `LICENSE`.
