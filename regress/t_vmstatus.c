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

/*
 * Feed every captured output to vmparse() through a memory stream and
 * check the counts and the lamp that vmlight() chooses.
 */
int
main(void)
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
	if (failed != 0)
		errx(1, "%d of %zu tests failed", failed, n);
	printf("%zu tests passed\n", n);
	return 0;
}
