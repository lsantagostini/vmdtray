# vmdtray

![License](https://img.shields.io/badge/License-ISC-blue.svg)
![Platform](https://img.shields.io/badge/Platform-OpenBSD-orange.svg)
![Language](https://img.shields.io/badge/C-Xlib%20%2B%20libdbus-lightgrey.svg)
![CI Status](https://github.com/lsantagostini/vmdtray/actions/workflows/ci.yml/badge.svg)

A traffic light for OpenBSD `vmd(8)` virtual machines in the system tray.

It is a small C program written the OpenBSD way: `style(9)`, `pledge(2)`
and `unveil(2)`, a manual page and a regress suite.  Clicking it opens
[vmm-consola](https://github.com/lsantagostini/vmm-consola), the VM
manager.

![Screenshot](screenshot.png)

## Features

* **Traffic light:** one look at the tray tells the state of every VM.

  | Lamp   | Meaning                                                        |
  |--------|----------------------------------------------------------------|
  | green  | every VM is running                                            |
  | yellow | some VMs are running and others are not, or one is stopping, paused or waiting |
  | red    | no VM is running, none is configured, or `vmd(8)` cannot be reached |

* **Tooltip:** hovering over it tells how many VMs are running, e.g.
  `1 of 2 VMs running`.
* **One click:** the left button starts the VM manager in its own session.
* **Native on any tray:** it speaks **StatusNotifierItem** over D-Bus when
  the desktop offers it (KDE Plasma and others), with a smooth icon in
  every size the panel may want, and falls back to the **XEmbed** system
  tray protocol on any other X11 tray.
* **Privilege dropping:** after connecting to the tray it `pledge`s to
  `stdio proc exec` and `unveil`s only `vmctl(8)` and the VM manager.

## Requirements

* **OS:** OpenBSD (targeting 8.0)
* **Packages:** `dbus` (libdbus, already there with KDE Plasma)
* **X11:** the `xbase` and `xshare` sets (Xlib and its headers)
* **Tools:** the base system compiler and `make`
* **Optional:** [vmm-consola](https://github.com/lsantagostini/vmm-consola)
  for the click action

## Installation

### 1. Build

```ksh
doas pkg_add dbus
git clone https://github.com/lsantagostini/vmdtray.git
cd vmdtray
make
```

### 2. Install

The program goes to `/usr/local/bin` and the manual to `/usr/local/man`:

```ksh
doas make install
man vmdtray
```

### 3. Start it with your session

Run it from your `~/.xsession`, or with a desktop autostart entry.  On KDE
Plasma, create `~/.config/autostart/vmdtray.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=vmdtray
Exec=/usr/local/bin/vmdtray
Terminal=false
NoDisplay=true
X-KDE-autostart-phase=2
```

## Usage

```
vmdtray [-x] [-c console] [-i interval]
```

* `-c console` — the program to start on a click.  Default:
  `/usr/local/bin/vmConsola`.  If vmm-consola is installed in your home
  directory, for example, use `-c $HOME/.local/bin/vmConsola`.
* `-i interval` — check the VMs every *interval* seconds, from 1 to 3600.
  Default: 5.
* `-x` — use the XEmbed protocol even if StatusNotifierItem is available.

The user running `vmdtray` must be allowed to run `vmctl status`, as the
`owner` of the VMs in `vm.conf(5)` is.

## How it works

| File         | Role                                                        |
|--------------|-------------------------------------------------------------|
| `vmdtray.c`  | options, `pledge`/`unveil`, main loop, `vmctl status`, click |
| `vmstatus.c` | parses `vmctl status`, picks the lamp, words the tooltip    |
| `icon.c`     | draws the traffic light into an antialiased ARGB32 image    |
| `sni.c`      | StatusNotifierItem over libdbus                             |
| `xembed.c`   | XEmbed system tray window over Xlib                         |

`vmstatus.c` and `icon.c` are plain C with no dependencies, which is what
the tests exercise.

## Tests

The regress suite follows `/usr/src/regress`:

```ksh
make -C regress regress
```

It runs:

* **`t_vmdtray`:**
  * 18 cases of `vmctl status` output (running, stopped, stopping,
    paused, waiting, unknown, no VMs, `vmd(8)` down, tabs, a VM named
    `running`, ...) checked against the parser and the lamp it picks;
  * the tooltip wording, singular and plural;
  * the icon in every size offered to the tray with every lamp lit:
    transparent corners, lit and unlit lamp colors, the housing, and the
    ARGB byte order (114 checks).
* **`style(9)` checks:** RCS id, 80 columns, no trailing whitespace, tabs
  for indentation.
* **`mandoc -Tlint -Wstyle`** on the manual page.

[CI](.github/workflows/ci.yml) builds and runs the suite on **OpenBSD 8.0**,
and builds the tests with `gcc -Wall -Wextra -Werror` on Ubuntu.

## Code style

The code follows `style(9)` (KNF) and the manual `mdoc(7)`: ISC license,
`$OpenBSD$` ids, local pathnames in `pathnames.h`, shared prototypes in
`extern.h`, `err(3)` for errors, and `bsd.prog.mk` with `WARNINGS=Yes`
building without a single warning.

## License

This project is released under the **ISC License**, aligned with the
OpenBSD philosophy.  See the [LICENSE](LICENSE) file for the full text.
