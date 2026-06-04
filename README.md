# MicroMount

[![License](https://img.shields.io/badge/license-GPL--3.0-0f172a?style=flat-square)](LICENSE)
[![Build](https://img.shields.io/badge/build-docker-2563eb?style=flat-square&logo=docker&logoColor=white)](./build.sh)
[![Platform](https://img.shields.io/badge/platform-PS5-1d4ed8?style=flat-square)](#overview)
[![GitHub Sponsors](https://img.shields.io/badge/Fund%20Development-GitHub%20Sponsors-e11d48?style=flat-square&logo=githubsponsors&logoColor=white)](https://github.com/sponsors/RenanGBarreto)

----

> [!IMPORTANT]
> ## 📦 Archive / Deprecation Notice
> MicroMount has served its purpose as a proof-of-concept payload to validate the new `.ffpfsc` format and gather testing feedback from the community.
>
> ✅ In collaboration with the author of [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus), the relevant features have now been integrated directly into ShadowMountPlus itself.
>
> 👉 If you are using **ShadowMountPlus `1.6test15-fix2` or newer**, you no longer need MicroMount:
> - https://github.com/drakmor/ShadowMountPlus/releases/tag/1.6test15-fix2
>
> ❤️ Thank you to everyone who tested, shared feedback, and trusted my work.
>
> 🗄️ This repository is now being archived, as MicroMount has fulfilled the goal it was created for.
>
> 💖 If you would like to support the work behind MicroMount and MkPFS, please consider sponsoring it here:
> - https://github.com/sponsors/RenanGBarreto
>
> 🔧 [MkPFS](https://github.com/PSBrew/MkPFS) will continue to be actively maintained. I am still fixing bugs there, and new releases are planned soon.

----

MicroMount is a PS5 payload that continuously scans configured folders for `.ffpfsc` images and mounts them automatically into managed mountpoints.

[Overview](#overview) · [Quick Start](#-quick-start) · [How it works](#how-it-works) · [Configuration](#configuration) · [Build](#build) · [Sponsorship](#-sponsorship)

## 📌 Overview

MicroMount runtime behavior:

- Scans configured directories for `.ffpfsc` files on a fixed interval.
- Derives a stable mount folder per image:
  - `${target_directory}/micromount-${GAMEID}-${HASH8}`
- Cleans stale managed mounts before each reconcile pass.
- Skips images that are already correctly mounted.
- Writes logs to `/data/micromount/debug.log` on every run.
- Reads config from `/data/micromount/config.ini` (auto-created from template when missing).

## 💖 Sponsorship

MkPFS is easier to sustain when users who benefit from it help fund it.

<p>
  <a href="https://github.com/sponsors/RenanGBarreto">
    <img alt="GitHub Sponsors" src="https://img.shields.io/badge/Fund%20Development-GitHub%20Sponsors-e11d48?style=flat-square&logo=githubsponsors&logoColor=white" />
  </a>
</p>

Sponsor here:
- https://github.com/sponsors/RenanGBarreto

Or donate directly using:
 - **BTC:**  **`141kKRoDpaS6PNC2yxSi8vziDFTmzCnArE`**
 - **USDT (TRC-20):**  **`TQb7bUYSYRmdWgALHCejH33dNij9XyTAnU`**
 - **USDT (ERC-20):**  **`0x63c0b4b21133c4068375ae7566dafcf1398cf6fb`**

## 🚀 Quick Start

1. Ensure your ShadowMountPlus `scan_depth=2` is set to `2`. Without it, games will not be recognized.
2. Convert your `.exfat` or `.ffpkg` games into `.ffpfsc` format using the `mkpfs` tool.

```bash
# Install using pip
pip install -U "mkpfs>=0.0.5"

# Convert an .exfat or .ffpkg file into a PFSC compressed image .ffpfsc
# NOTE: .ffpfsc is the simplest way to have game-file compression support.
mkpfs pack file --compress --verify ./GAME1234.exfat ./GAME1234.ffpfsc
```

3. Ensure the new `.ffpfsc` files are uploaded to the PS5, in one of the monitored folders like `/data/homebrew`.
4. Send or activate the `micromount.elf` payload.

## ⚙️ How it works

1. MicroMount starts, loads defaults, creates `/data/micromount/config.ini` from the embedded template when missing, then loads it.
2. It scans `scan_paths` recursively up to `scan_depth` for `.ffpfsc`.
3. For each candidate image, it builds:
   - `GAMEID` from filename (PlayStation-style `AAAA0000` or `AAAA00000` when found).
   - Fallback ID from first 10 alphanumeric chars in filename (uppercased) if no title ID exists.
   - `HASH8` as first 8 hex chars of SHA-256 of the full source path.
4. It runs cleanup for managed mount folders (`micromount-*`) that are stale or empty.
5. It mounts missing candidates with the configured LVD/PFS profile.
6. It emits a summary notification and repeats after `scan_interval_seconds`.

## 📂 Paths

- Config file: `/data/micromount/config.ini`
- Log file: `/data/micromount/debug.log`
- Runtime root: `/data/micromount`
- Default target directory: `/data/homebrew`

## 🛠️ Configuration

Use `config.ini.example` as the template for `/data/micromount/config.ini`.
If `/data/micromount/config.ini` does not exist at runtime, MicroMount automatically creates it from the embedded `config.ini.example`.

Core keys:

- `target_directory` (default: `/data/homebrew`)
- `scanpath` (repeatable)
- `scan_paths` (comma/semicolon-separated list)
- `scan_depth` (default: `1`)
- `scan_interval_seconds` (default: `30`)
- `debug` (default: `1`)

Mount profile keys:

- `lvd_image_type`
- `lvd_sector_size`
- `lvd_secondary_unit`
- `lvd_raw_flags`
- `pfs_fstype`
- `pfs_mkeymode`
- `pfs_budgetid`
- `pfs_sigverify`
- `pfs_playgo`
- `pfs_disc`
- `pfs_use_ekpfs`
- `pfs_read_only`
- `pfs_force`

Default mount family:

```text
{0, 65536, true, 0x9, "pfs", "AC", "system", true, false}
```

(`lvd_secondary_unit` defaults to sector size; `pfs_use_ekpfs=1`; `pfs_read_only=1`.)

## 🧾 Notifications and logs

- Logs are always written to `/data/micromount/debug.log`.
- `debug=1` enables detailed per-image notifications.
- `debug=0` keeps major summary notifications while suppressing debug popups.

## 🏗️ Build

Build with Docker (recommended in this repo):

```bash
./build.sh
```

Output artifact:

```text
./micromount.elf
```

Local make (requires PS5 payload SDK):

```bash
make clean all
```


## 💙 Special thanks and contributors

Special thanks to the people and communities helping shape MkPFS:

- **Renan @ PSBrew**: main creator and maintainer
- **Darkmor @ ShadowMountPlus**: creator of [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus), whose work helped inspire practical PFS mounting workflows
- **The PlayStation and reverse-engineering community**: for tools, research threads, testing feedback, technical notes, and historical knowledge
- **Community-maintained references and wiki pages**: especially the projects and archives that preserve PFS, PKG, and FPKG implementation details

## 🔗 Related projects

- [MKPFS](https://github.com/PSBrew/MkPFS): The main project repository for the MkPFS CLI, Python library, and documentation.
- [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus): Practical PS5 auto-mounter and a key reference for `pfs` compatibility
- [PSDevWiki PFS](https://www.psdevwiki.com/ps4/PFS): Community reference for PFS on-disk structures
- [PSDevWiki PKG files](https://www.psdevwiki.com/ps4/PKG_files): PKG format reference and tooling pointers
- [ShadPKG HOWWORKS](https://github.com/seregonwar/ShadPKG/blob/main/docs/HOWWORKS.md): Implementation-focused PKG/PFS decryption walkthrough
- [Wololo: PS4 FPKG writeup by Flatz](https://wololo.net/ps4-fpkg-writeup-by-flatz/): Historical writeup on FPKG/PKG techniques
