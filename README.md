# CrossPoint Plus

> **Personal fork.** CrossPoint Plus is my own experimental fork of
> [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) — a
> playground for tweaks, learning, and personal features. It is **not** an
> official CrossPoint release and is not affiliated with the CrossPoint project,
> Xteink, or any device manufacturer. For the stable, community-maintained
> firmware, use upstream.

CrossPoint Plus is open-source e-reader firmware for the ESP32-C3-based Xteink
[X4](https://www.xteink.com/products/xteink-x4) and
[X3](https://www.xteink.com/products/xteink-x3), built on top of CrossPoint
Reader. The reading experience is lightweight and tuned for very constrained
hardware (~380KB RAM, single 48KB framebuffer, 800×480 monochrome E-Ink).

> **Tested hardware:** this fork has only been tested on the Xteink **X4**. The
> **X3** is supported upstream but **untested here** — use at your own risk.

---

## What it does

Inherited from CrossPoint, the firmware includes:

- **Reader engine** — EPUB 2/3 rendering, image handling, hyphenation, kerning,
  chapter navigation, footnotes, bookmarks, go-to-percent, auto page turn,
  orientation control, focus reading, and KOReader progress sync.
- **Formats** — `.epub`, `.xtc/.xtch`, `.txt`, and `.bmp`.
- **Custom fonts** — load your own `.cpfont` files from the SD card.
- **Library workflow** — folder browser, recent books, long-press delete, SD cache management.
- **Wireless** — file-transfer web UI, web settings, WebDAV, Calibre connect, OPDS browser, and OTA updates.
- **Customization** — multiple themes, sleep screens, button remapping, refresh cadence.
- **Localization** — 24 UI languages with RTL support.

> My fork-specific changes (if any) are tracked in the commit history and
> [CHANGELOG](./CHANGELOG.md) *(create when there's something to note)*.

---

## Building from source

### Prerequisites

- [pioarduino](https://github.com/pioarduino/pioarduino) (or VS Code + the pioarduino plugin)
- Python 3.8+
- `clang-format` 21
- A USB-C cable that supports data transfer

### Setup

```bash
git clone --recursive git@github.com:tanpinghua/crosspoint_plus.git
cd crosspoint_plus

# if cloned without --recursive:
git submodule update --init --recursive
```

### Build / flash / monitor

```bash
pio run --target upload          # build + flash
python3 scripts/debugging_monitor.py   # serial log monitor (Linux/macOS)
```

### Pre-commit checks

```bash
./bin/clang-format-fix
pio check -e default
pio run -e default
```

---

## Internals

The firmware caches aggressively to the SD card to keep RAM pressure low. The
cache lives at `.crosspoint/` on the card, one directory per book (keyed by
content hash) holding reading progress, cover, metadata, and per-chapter layout.
Deleting `/.crosspoint` clears all caches and forces a clean re-parse.

See the [file formats document](./docs/file-formats.md) for the binary layouts,
and the [User Guide](./USER_GUIDE.md) for device usage.

---

## Staying in sync with upstream

```bash
git fetch upstream
git merge upstream/master   # or rebase, depending on preference
```

---

## Credits

CrossPoint Plus stands entirely on the work of others:

- [**CrossPoint Reader**](https://github.com/crosspoint-reader/crosspoint-reader)
  — the upstream firmware this fork is based on. All credit for the engine,
  rendering, and wireless stack goes to the CrossPoint community.
- [**diy-esp32-epub-reader**](https://github.com/atomic14/diy-esp32-epub-reader)
  — the project that inspired CrossPoint.

CrossPoint Plus is **not affiliated with Xteink or any device manufacturer**, nor
with the official CrossPoint project. Use at your own risk on a personal device.
