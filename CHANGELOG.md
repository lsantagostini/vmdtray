# Changelog

## v1.0.0 — 2026-11-13

First release.

* Traffic light for `vmd(8)` virtual machines in the X11 system tray
  (XEmbed system tray protocol; works on KDE Plasma through
  `xembedsniproxy`).
* Green when every VM is running, yellow when only some are or one is
  stopping, paused or waiting, red when none is running or `vmd(8)` cannot
  be reached.
* Left click starts the VM manager
  ([vmm-consola](https://github.com/lsantagostini/vmm-consola) by
  default, `-c` to choose another) in its own session.
* Checks `vmctl status` every 5 seconds (`-i` to change it); the window
  title, shown as tooltip, tells how many VMs are running.
* Transparent background on 32-bit (ARGB) visuals.
* `pledge(2)` to `stdio proc exec` and `unveil(2)` limited to `vmctl(8)`
  and the VM manager.
* Written in C with Xlib following `style(9)`; manual page in `mdoc(7)`.
* Regress suite: 18 parser and traffic light cases, `style(9)` checks and
  `mandoc -Tlint`; CI on OpenBSD 8.0 and Ubuntu.
