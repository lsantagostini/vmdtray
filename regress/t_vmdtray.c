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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

/*
 * The lines below are "vmctl status" output as printed on OpenBSD 8.0,
 * split in two strings only to fit in 80 columns.
 */
#define HEADER	"   ID   PID VCPUS  MAXMEM  CURMEM     TTY"		\
		"        OWNER    STATE NAME\n"
#define RUNNING	"    1 64692     4   12.0G    2.8G   ttyp0"		\
		"     leonardo  running debian\n"
#define STOPPED	"    1     -     4   12.0G       -       -"		\
		"     leonardo  stopped debian\n"
#define OTHER(state)							\
		"    2 81204     1    512M    498M   ttyp1"		\
		"         root " state " obsd\n"

/* A captured or crafted "vmctl status" output and what it must yield. */
struct test {
	const char	*name;
	const char	*output;
	int		 total;
	int		 up;
	int		 lamp;
};

static const struct test tests[] = {
	{ "one running", HEADER RUNNING, 1, 1, LIGHT_GREEN },
	{ "one stopped", HEADER STOPPED, 1, 0, LIGHT_RED },
	{ "all running", HEADER RUNNING OTHER(" running"), 2, 2,
	    LIGHT_GREEN },
	{ "some running", HEADER RUNNING OTHER(" stopped"), 2, 1,
	    LIGHT_YELLOW },
	{ "none running", HEADER STOPPED OTHER(" stopped"), 2, 0,
	    LIGHT_RED },
	{ "running and stopping", HEADER RUNNING OTHER("stopping"), 2, 1,
	    LIGHT_YELLOW },
	{ "running and paused", HEADER RUNNING OTHER("  paused"), 2, 1,
	    LIGHT_YELLOW },
	{ "running and waiting", HEADER RUNNING OTHER(" waiting"), 2, 1,
	    LIGHT_YELLOW },
	{ "running and unknown", HEADER RUNNING OTHER(" unknown"), 2, 1,
	    LIGHT_YELLOW },
	{ "owner with group",
	    HEADER "    1 64692     4   12.0G    2.8G   ttyp0"
	    " leonardo:wheel running debian\n", 1, 1, LIGHT_GREEN },
	{ "tabs and carriage returns",
	    HEADER "\t1\t64692\t4\t12.0G\t2.8G\tttyp0\tleonardo\trunning"
	    "\tdebian\r\n", 1, 1, LIGHT_GREEN },
	{ "no final newline",
	    HEADER "    1 64692     4   12.0G    2.8G   ttyp0"
	    "     leonardo  running debian", 1, 1, LIGHT_GREEN },
	{ "header only", HEADER, 0, 0, LIGHT_RED },
	{ "empty", "", 0, 0, LIGHT_RED },
	{ "vmd not running",
	    "vmctl: connect: /var/run/vmd.sock: No such file or directory\n",
	    0, 0, LIGHT_RED },
	{ "error between rows",
	    HEADER RUNNING "vmctl: some warning\n" OTHER(" running"), 2, 2,
	    LIGHT_GREEN },
	{ "VM named running",
	    HEADER "    1     -     4   12.0G       -       -"
	    "     leonardo  stopped running\n", 1, 0, LIGHT_RED },
	{ "VM named ID",
	    HEADER "    1 64692     4   12.0G    2.8G   ttyp0"
	    "     leonardo  running ID\n", 1, 1, LIGHT_GREEN },
};

/* What vmdescribe() must say for some counts. */
struct describetest {
	struct vmcount	 vc;
	const char	*text;
};

static const struct describetest describetests[] = {
	{ { 0, 0 }, "no VMs configured" },
	{ { 1, 1 }, "1 of 1 VM running" },
	{ { 1, 0 }, "0 of 1 VM running" },
	{ { 2, 1 }, "1 of 2 VMs running" },
	{ { 3, 0 }, "0 of 3 VMs running" },
};

/* Icon sizes the tray is offered. */
static const int iconsizes[] = { 16, 22, 32, 48, 64 };

static int	 testdescribe(void);
static int	 testicon(void);
static int	 testparse(void);
static int	 checkpixel(const unsigned char *, int, int, int, int,
		    unsigned int, const char *);

/*
 * Run every group of tests and report how many cases passed.  Exit 1 if
 * any failed.
 */
int
main(void)
{
	int failed;

	failed = testparse() + testdescribe() + testicon();
	if (failed != 0)
		errx(1, "%d tests failed", failed);
	printf("all tests passed\n");
	return 0;
}

/*
 * Feed every captured output to vmparse() through a memory stream and
 * check the counts and the lamp that vmlight() chooses.  Return the
 * number of failed cases.
 */
static int
testparse(void)
{
	struct vmcount vc;
	const struct test *t;
	FILE *fp;
	size_t i, n;
	int failed, lamp;

	failed = 0;
	n = sizeof(tests) / sizeof(tests[0]);
	for (i = 0; i < n; i++) {
		t = &tests[i];
		/*
		 * Include the terminating NUL: fmemopen(3) may refuse an
		 * empty buffer, and vmparse() ignores the extra empty line.
		 */
		if ((fp = fmemopen((void *)t->output, strlen(t->output) + 1,
		    "r")) == NULL)
			err(1, "fmemopen");
		if (vmparse(fp, &vc) == -1)
			err(1, "%s: vmparse", t->name);
		fclose(fp);
		lamp = vmlight(&vc);
		if (vc.total != t->total || vc.up != t->up ||
		    lamp != t->lamp) {
			warnx("%s: got %d of %d, lamp %d; want %d of %d, "
			    "lamp %d", t->name, vc.up, vc.total, lamp,
			    t->up, t->total, t->lamp);
			failed++;
		}
	}
	printf("vmparse/vmlight: %zu of %zu passed\n", n - failed, n);
	return failed;
}

/* Check the words vmdescribe() uses.  Return the number of failures. */
static int
testdescribe(void)
{
	char buf[128];
	size_t i, n;
	int failed;

	failed = 0;
	n = sizeof(describetests) / sizeof(describetests[0]);
	for (i = 0; i < n; i++) {
		vmdescribe(&describetests[i].vc, buf, sizeof(buf));
		if (strcmp(buf, describetests[i].text) != 0) {
			warnx("vmdescribe: got \"%s\"; want \"%s\"", buf,
			    describetests[i].text);
			failed++;
		}
	}
	printf("vmdescribe: %zu of %zu passed\n", n - failed, n);
	return failed;
}

/*
 * Render the icon in every size with every lamp lit and look at a few
 * pixels: the corners are transparent, the center of each lamp has its
 * lit or unlit color at full opacity, and so has the housing beside the
 * top lamp where the geometry falls on whole pixels.  Return the number
 * of failed checks.
 */
static int
testicon(void)
{
	unsigned char *buf;
	double cy, top;
	size_t i;
	int checks, d, failed, gap, j, lamp, size, x;

	checks = failed = 0;
	for (i = 0; i < sizeof(iconsizes) / sizeof(iconsizes[0]); i++) {
		size = iconsizes[i];
		if ((buf = calloc(size * size, 4)) == NULL)
			err(1, NULL);
		if ((gap = size / 22) < 1)
			gap = 1;
		d = (size - 4 * gap) / 3;
		top = (size - (3 * d + 4 * gap)) / 2.0;
		for (lamp = 0; lamp < LIGHT_MAX; lamp++) {
			iconrender(buf, size, lamp);
			checks += 4;
			failed += checkpixel(buf, size, 0, 0, 0, 0, "corner");
			failed += checkpixel(buf, size, size - 1, size - 1,
			    0, 0, "corner");
			failed += checkpixel(buf, size, size - 1, 0, 0, 0,
			    "corner");
			failed += checkpixel(buf, size, 0, size - 1, 0, 0,
			    "corner");
			for (j = 0; j < LIGHT_MAX; j++) {
				cy = top + gap + j * (d + gap) + d / 2.0;
				checks++;
				failed += checkpixel(buf, size, size / 2,
				    (int)cy, 0xff, j == lamp ? colorlit[j] :
				    colorunlit[j], "lamp center");
			}
			x = size / 2 - d / 2 - gap;
			if (d % 2 == 0 && top == (int)top) {
				checks++;
				failed += checkpixel(buf, size, x,
				    (int)(top + gap + d / 2.0), 0xff,
				    colorhousing, "housing");
			}
		}
		free(buf);
	}
	printf("iconrender: %d of %d passed\n", checks - failed, checks);
	return failed;
}

/*
 * Check that pixel (x, y) has the given alpha and, if it is not fully
 * transparent, the given 0xrrggbb color.  Return 1 if it does not.
 */
static int
checkpixel(const unsigned char *buf, int size, int x, int y, int alpha,
    unsigned int rgb, const char *what)
{
	const unsigned char *p;

	p = buf + 4 * (y * size + x);
	if (p[0] == alpha && (alpha == 0 || (p[1] == (rgb >> 16 & 0xff) &&
	    p[2] == (rgb >> 8 & 0xff) && p[3] == (rgb & 0xff))))
		return 0;
	warnx("iconrender %dx%d %s (%d,%d): got %02x%02x%02x%02x; "
	    "want %02x%06x", size, size, what, x, y, p[0], p[1], p[2], p[3],
	    alpha, alpha == 0 ? 0 : rgb);
	return 1;
}
