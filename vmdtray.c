/*	$OpenBSD$	*/

/*
 * Copyright (c) 2026 Leonardo Santagostini <leonardo@santagostini.com.ar>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "extern.h"
#include "pathnames.h"

#define SYSTEM_TRAY_REQUEST_DOCK	0
#define XEMBED_MAPPED			(1 << 0)

static const char *lit[LIGHT_MAX] = { "#e53935", "#fdd835", "#43a047" };
static const char *unlit[LIGHT_MAX] = { "#2a0e0e", "#2a270e", "#0e2410" };

static Display		*dpy;
static Window		 manager, root, win;
static GC		 gc;
static Atom		 managersel, opcode, traysel, xembedinfo;
static Colormap		 cmap;
static const char	*console = _PATH_CONSOLE;
static unsigned long	 alpha, housing, on[LIGHT_MAX], off[LIGHT_MAX];
static int		 current, height, width;

static int		 vmstatus(char *, size_t);
static void		 dock(void);
static void		 draw(void);
static void		 handle(XEvent *);
static void		 launch(void);
static unsigned long	 pixel(const char *);
static __dead void	 usage(void);

/*
 * vmdtray shows the state of the vmd(8) virtual machines as a traffic
 * light in the system tray: green when every VM is running, yellow when
 * only some are or one is changing state, and red when none is running
 * or vmd(8) cannot be reached.  Clicking it starts the VM manager.
 */
int
main(int argc, char *argv[])
{
	XEvent ev;
	char title[128], traystr[32];
	XSetWindowAttributes swa;
	XVisualInfo vi;
	XClassHint class;
	long info[2];
	struct timespec left, next, now;
	const char *errstr;
	struct pollfd pfd;
	Visual *visual;
	int ch, depth, i, interval, scr, timeout;

	interval = 5;
	while ((ch = getopt(argc, argv, "c:i:")) != -1) {
		switch (ch) {
		case 'c':
			console = optarg;
			break;
		case 'i':
			interval = strtonum(optarg, 1, 3600, &errstr);
			if (errstr != NULL)
				errx(1, "interval is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		usage();

	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "cannot open display");
	if (fcntl(ConnectionNumber(dpy), F_SETFD, FD_CLOEXEC) == -1)
		err(1, "fcntl");
	scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);

	(void)snprintf(traystr, sizeof(traystr), "_NET_SYSTEM_TRAY_S%d", scr);
	traysel = XInternAtom(dpy, traystr, False);
	managersel = XInternAtom(dpy, "MANAGER", False);
	opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	xembedinfo = XInternAtom(dpy, "_XEMBED_INFO", False);

	/*
	 * With a 32-bit visual the window has an alpha channel and its
	 * background can be transparent, so the tray shows through around
	 * the traffic light.  Otherwise borrow the background of the parent.
	 */
	if (XMatchVisualInfo(dpy, scr, 32, TrueColor, &vi) != 0) {
		visual = vi.visual;
		depth = vi.depth;
		cmap = XCreateColormap(dpy, root, visual, AllocNone);
		alpha = 0xffUL << 24;
	} else {
		visual = DefaultVisual(dpy, scr);
		depth = DefaultDepth(dpy, scr);
		cmap = DefaultColormap(dpy, scr);
		alpha = 0;
	}

	housing = pixel("#0c0c0c");
	for (i = 0; i < LIGHT_MAX; i++) {
		on[i] = pixel(lit[i]);
		off[i] = pixel(unlit[i]);
	}

	width = height = 22;
	swa.background_pixel = 0;
	swa.border_pixel = 0;
	swa.colormap = cmap;
	win = XCreateWindow(dpy, root, 0, 0, width, height, 0, depth,
	    InputOutput, visual, CWBackPixel | CWBorderPixel | CWColormap,
	    &swa);
	if (alpha == 0)
		XSetWindowBackgroundPixmap(dpy, win, ParentRelative);
	XSelectInput(dpy, win, ButtonPressMask | ExposureMask |
	    StructureNotifyMask);
	XSelectInput(dpy, root, StructureNotifyMask);
	gc = XCreateGC(dpy, win, 0, NULL);

	class.res_name = "vmdtray";
	class.res_class = "Vmdtray";
	XSetClassHint(dpy, win, &class);
	XStoreName(dpy, win, "vmd");

	/* XEmbed protocol version 0, and ask to be shown once docked. */
	info[0] = 0;
	info[1] = XEMBED_MAPPED;
	XChangeProperty(dpy, win, xembedinfo, xembedinfo, 32,
	    PropModeReplace, (unsigned char *)info, 2);

	/*
	 * From here on only the open display connection, vmctl(8) and the
	 * VM manager are needed.
	 */
	if (unveil(_PATH_VMCTL, "x") == -1)
		err(1, "unveil %s", _PATH_VMCTL);
	if (unveil(console, "x") == -1)
		err(1, "unveil %s", console);
	if (pledge("stdio proc exec", NULL) == -1)
		err(1, "pledge");

	dock();

	if (clock_gettime(CLOCK_MONOTONIC, &next) == -1)
		err(1, "clock_gettime");
	pfd.fd = ConnectionNumber(dpy);
	pfd.events = POLLIN;
	for (;;) {
		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
			err(1, "clock_gettime");
		if (timespeccmp(&now, &next, >=)) {
			current = vmstatus(title, sizeof(title));
			XStoreName(dpy, win, title);
			draw();
			next = now;
			next.tv_sec += interval;
		}

		while (XPending(dpy) > 0) {
			XNextEvent(dpy, &ev);
			handle(&ev);
		}

		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
			err(1, "clock_gettime");
		if (timespeccmp(&now, &next, >=))
			continue;
		timespecsub(&next, &now, &left);
		timeout = left.tv_sec * 1000 + left.tv_nsec / 1000000 + 1;
		if (poll(&pfd, 1, timeout) == -1 && errno != EINTR)
			err(1, "poll");
	}
}

/*
 * Run "vmctl status" and count the VMs that are running.  Return the lamp
 * to light and describe the state in title.
 */
static int
vmstatus(char *title, size_t len)
{
	struct vmcount vc;
	FILE *fp;
	pid_t pid;
	int fd[2], status;

	if (pipe(fd) == -1)
		err(1, "pipe");
	switch ((pid = fork())) {
	case -1:
		err(1, "fork");
	case 0:
		if (dup2(fd[1], STDOUT_FILENO) == -1 ||
		    dup2(fd[1], STDERR_FILENO) == -1)
			_exit(1);
		close(fd[0]);
		close(fd[1]);
		execl(_PATH_VMCTL, "vmctl", "status", (char *)NULL);
		_exit(127);
	}
	close(fd[1]);
	if ((fp = fdopen(fd[0], "r")) == NULL)
		err(1, "fdopen");
	if (vmparse(fp, &vc) == -1)
		err(1, "read vmctl output");
	fclose(fp);

	while (waitpid(pid, &status, 0) == -1)
		if (errno != EINTR)
			err(1, "waitpid");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		(void)snprintf(title, len, "vmd: not available");
		return LIGHT_RED;
	}

	(void)snprintf(title, len, "vmd: %d of %d VMs running", vc.up,
	    vc.total);
	return vmlight(&vc);
}

/*
 * Ask the system tray manager, if there is one, to embed our window.
 * Without a manager, wait for its MANAGER announcement on the root window.
 */
static void
dock(void)
{
	XEvent ev;

	XGrabServer(dpy);
	if ((manager = XGetSelectionOwner(dpy, traysel)) != None)
		XSelectInput(dpy, manager, StructureNotifyMask);
	XUngrabServer(dpy);
	if (manager == None) {
		XFlush(dpy);
		return;
	}

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = manager;
	ev.xclient.message_type = opcode;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = CurrentTime;
	ev.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
	ev.xclient.data.l[2] = win;
	XSendEvent(dpy, manager, False, NoEventMask, &ev);
	XFlush(dpy);
}

/*
 * Draw the traffic light: a dark housing with three lamps, of which only
 * the current one is lit.
 */
static void
draw(void)
{
	int d, gap, i, size, x, y;

	XClearWindow(dpy, win);
	size = width < height ? width : height;
	if ((gap = size / 22) < 1)
		gap = 1;
	d = (size - 4 * gap) / 3;
	x = (width - d) / 2;
	y = (height - (3 * d + 4 * gap)) / 2;

	XSetForeground(dpy, gc, housing);
	XFillRectangle(dpy, win, gc, x - gap, y, d + 2 * gap,
	    3 * d + 4 * gap);
	for (i = 0; i < LIGHT_MAX; i++) {
		XSetForeground(dpy, gc, i == current ? on[i] : off[i]);
		XFillArc(dpy, win, gc, x, y + gap + i * (d + gap), d, d,
		    0, 360 * 64);
	}
	XFlush(dpy);
}

/*
 * Start the VM manager on a left click, follow the size the tray gives
 * us, redraw when needed and dock again if the tray manager is replaced.
 */
static void
handle(XEvent *ev)
{
	switch (ev->type) {
	case ButtonPress:
		if (ev->xbutton.button == Button1)
			launch();
		break;
	case ClientMessage:
		/* A new tray manager took over: dock again. */
		if (ev->xclient.message_type == managersel &&
		    (Atom)ev->xclient.data.l[1] == traysel)
			dock();
		break;
	case ConfigureNotify:
		if (ev->xconfigure.window == win) {
			width = ev->xconfigure.width;
			height = ev->xconfigure.height;
			draw();
		}
		break;
	case DestroyNotify:
		if (ev->xdestroywindow.window == manager)
			manager = None;
		break;
	case Expose:
		if (ev->xexpose.count == 0)
			draw();
		break;
	}
}

/*
 * Start the VM manager in its own session.  The intermediate child exits
 * at once, so the manager is not ours to wait for and outlives us.
 */
static void
launch(void)
{
	pid_t pid;
	int status;

	switch ((pid = fork())) {
	case -1:
		warn("fork");
		return;
	case 0:
		switch (fork()) {
		case -1:
			_exit(1);
		case 0:
			if (setsid() == -1)
				_exit(1);
			execl(console, console, (char *)NULL);
			warn("%s", console);
			_exit(127);
		default:
			_exit(0);
		}
	}
	while (waitpid(pid, &status, 0) == -1)
		if (errno != EINTR) {
			warn("waitpid");
			break;
		}
}

/*
 * Allocate a color given by name or as "#rrggbb" and return its pixel,
 * fully opaque when the window has an alpha channel.
 */
static unsigned long
pixel(const char *name)
{
	XColor exact, screen;

	if (XAllocNamedColor(dpy, cmap, name, &screen, &exact) == 0)
		errx(1, "cannot allocate color %s", name);
	return screen.pixel | alpha;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-c console] [-i interval]\n",
	    getprogname());
	exit(1);
}
