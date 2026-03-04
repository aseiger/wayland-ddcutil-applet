# bar_applet – wf-panel-pi Display Settings Plugin

A lightweight **wf-panel-pi** plugin for **Raspberry Pi OS** (Wayland) that
provides quick access to **brightness** and **volume** controls directly from
the taskbar, using **DDC/CI** to talk to the monitor over I²C.

## Features

- **Brightness slider** – reads/writes the monitor's brightness via DDC/CI
  (VCP feature `0x10`) using `ddcutil`.
- **Volume slider** – reads/writes the monitor's built-in speaker volume via
  DDC/CI (VCP feature `0x62`) using `ddcutil`.
- **lcdstats integration** – sends brightness and volume updates to a local
  `lcdstats` daemon over a Unix domain socket (`/tmp/lcdstats.sock`), keeping
  an external LCD status display in sync.
- Single-click popup that auto-dismisses on click-outside.
- Minimal dependencies – GTKmm 3.0 and the wf-panel-pi SDK.

## Prerequisites

Install the build dependencies on your Raspberry Pi:

```bash
sudo apt update
sudo apt install build-essential pkg-config wf-panel-pi-dev libgtkmm-3.0-dev ddcutil
```

`ddcutil` is also needed at runtime for brightness and volume control.

## Building

```bash
make
```

This produces `libbar_applet.so` in the project root.

## Installing

```bash
sudo make install
```

Then restart the panel:

```bash
killall wf-panel-pi; wf-panel-pi &
```

Add `bar_applet` to your panel configuration to see the **Display Settings**
icon in the taskbar.

## Uninstalling

```bash
sudo make uninstall
killall wf-panel-pi; wf-panel-pi &
```

## Project Structure

```
bar_applet/
├── data/
│   └── bar_applet.desktop    # Plugin descriptor
├── src/
│   ├── bar_applet.cpp        # Main plugin (UI, wf-panel-pi integration)
│   ├── brightness.c          # Brightness backend (DDC/CI via ddcutil)
│   ├── brightness.h
│   ├── lcdstats.c            # lcdstats daemon client (Unix socket, JSON)
│   ├── lcdstats.h
│   ├── volume.c              # Volume backend (DDC/CI via ddcutil)
│   └── volume.h
├── Makefile
└── README.md
```

## How It Works

| Component  | Mechanism |
|------------|-----------|
| Brightness | `ddcutil getvcp 10` / `ddcutil setvcp 10 <value>` — DDC/CI VCP 0x10 (0–100). |
| Volume     | `ddcutil getvcp 62` / `ddcutil setvcp 62 <value>` — DDC/CI VCP 0x62 (0–100). |
| lcdstats   | Sends `{"type": "brightness", "value": N}` / `{"type": "volume", "value": N}` as newline-delimited JSON over a Unix stream socket at `/tmp/lcdstats.sock`. Reconnects automatically. |

### DDC/CI Permissions

`ddcutil` communicates over the `/dev/i2c-*` device nodes. By default these
are accessible only to root. To allow your user to run `ddcutil` without
`sudo`, add yourself to the `i2c` group:

```bash
sudo usermod -aG i2c $USER
```

Then log out and back in (or reboot). Verify with:

```bash
ddcutil detect
```

## License

MIT
