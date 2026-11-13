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

/*
 * The freedesktop.org system tray protocol over XEmbed: a small X window
 * that the tray manager embeds.
 */

#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "extern.h"

#define SYSTEM_TRAY_REQUEST_DOCK	0
#define XEMBED_MAPPED			(1 << 0)

static Display		*dpy;
static Window		 manager, root, win;
static GC		 gc;
static Atom		 managersel, opcode, traysel, xembedinfo;
static Colormap		 cmap;
static unsigned long	 alpha, housing, on[LIGHT_MAX], off[LIGHT_MAX];
static int		 current, height, width;

static void		 dock(void);
static void		 draw(void);
static void		 handle(XEvent *);
static unsigned long	 pixel(unsigned int);
static void		 xembed_dispatch(void);
static int		 xembed_fd(void);
static void		 xembed_update(int, const char *);

/*
 * Connect to the X server, create the window and ask the tray manager to
 * embed it.  Fill in t and return 0.
 */
int
xembed_init(struct tray *t)
{
	XSetWindowAttributes swa;
	XVisualInfo vi;
	XClassHint class;
	char traystr[32];
	long info[2];
	Visual *visual;
	int depth, i, scr;

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

	housing = pixel(colorhousing);
	for (i = 0; i < LIGHT_MAX; i++) {
		on[i] = pixel(colorlit[i]);
		off[i] = pixel(colorunlit[i]);
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

	dock();

	t->fd = xembed_fd;
	t->dispatch = xembed_dispatch;
	t->update = xembed_update;
	return 0;
}

static int
xembed_fd(void)
{
	return ConnectionNumber(dpy);
}

/* Handle every event already received. */
static void
xembed_dispatch(void)
{
	XEvent ev;

	while (XPending(dpy) > 0) {
		XNextEvent(dpy, &ev);
		handle(&ev);
	}
}

/*
 * Light the given lamp and put the description in the window title, which
 * some trays show as a tooltip.
 */
static void
xembed_update(int lamp, const char *text)
{
	char title[160];

	current = lamp;
	(void)snprintf(title, sizeof(title), "vmd: %s", text);
	XStoreName(dpy, win, title);
	draw();
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
 * Allocate a color given as 0xrrggbb and return its pixel, fully opaque
 * when the window has an alpha channel.
 */
static unsigned long
pixel(unsigned int rgb)
{
	XColor c;

	c.red = (rgb >> 16 & 0xff) * 0x101;
	c.green = (rgb >> 8 & 0xff) * 0x101;
	c.blue = (rgb & 0xff) * 0x101;
	c.flags = DoRed | DoGreen | DoBlue;
	if (XAllocColor(dpy, cmap, &c) == 0)
		errx(1, "cannot allocate color #%06x", rgb);
	return c.pixel | alpha;
}
