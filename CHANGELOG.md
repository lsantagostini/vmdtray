# Changelog

## v1.1.0 — 2026-11-13

* **StatusNotifierItem:** when the session bus has a StatusNotifierItem
  watcher (KDE Plasma and others), vmdtray registers a native item over
  D-Bus instead of docking an X window.  The tray now shows a **tooltip**
  with the number of VMs running: through `xembedsniproxy` the XEmbed
  window had none, since the proxy does not publish the `ToolTip`
  property.
* The icon is drawn in memory with antialiasing and offered in 16, 22,
  32, 48 and 64 pixels, so it is smooth at any panel size.
* XEmbed is still used on any other X11 tray, or always with the new
  `-x` flag.
* The tooltip says "1 of 1 VM running" or "1 of 2 VMs running", and
  "no VMs configured" when there are none.
* Split into modules: `icon.c`, `sni.c`, `vmstatus.c` and `xembed.c`.
* New dependency: libdbus (`dbus` package).
* Regress suite: tooltip wording and 114 checks of the rendered icon on
  top of the 18 parser cases.

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
