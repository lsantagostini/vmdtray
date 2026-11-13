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
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"
#include "pathnames.h"

static const char	*console = _PATH_CONSOLE;

static int		 vmstatus(char *, size_t);
static __dead void	 usage(void);

/*
 * vmdtray shows the state of the vmd(8) virtual machines as a traffic
 * light in the system tray: green when every VM is running, yellow when
 * only some are or one is changing state, and red when none is running
 * or vmd(8) cannot be reached.  Clicking it starts the VM manager.  It
 * speaks StatusNotifierItem when the desktop offers it, which also gives
 * a tooltip, and the XEmbed system tray protocol otherwise.
 */
int
main(int argc, char *argv[])
{
	struct tray tray;
	char text[128];
	struct timespec left, next, now;
	const char *errstr;
	struct pollfd pfd;
	int ch, interval, lamp, timeout, xflag;

	interval = 5;
	xflag = 0;
	while ((ch = getopt(argc, argv, "c:i:x")) != -1) {
		switch (ch) {
		case 'c':
			console = optarg;
			break;
		case 'i':
			interval = strtonum(optarg, 1, 3600, &errstr);
			if (errstr != NULL)
				errx(1, "interval is %s: %s", errstr, optarg);
			break;
		case 'x':
			xflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		usage();

	if (xflag || sni_init(&tray) == -1)
		(void)xembed_init(&tray);

	/*
	 * From here on only the open tray connection, vmctl(8) and the VM
	 * manager are needed.
	 */
	if (unveil(_PATH_VMCTL, "x") == -1)
		err(1, "unveil %s", _PATH_VMCTL);
	if (unveil(console, "x") == -1)
		err(1, "unveil %s", console);
	if (pledge("stdio proc exec", NULL) == -1)
		err(1, "pledge");

	if (clock_gettime(CLOCK_MONOTONIC, &next) == -1)
		err(1, "clock_gettime");
	pfd.fd = tray.fd();
	pfd.events = POLLIN;
	for (;;) {
		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
			err(1, "clock_gettime");
		if (timespeccmp(&now, &next, >=)) {
			lamp = vmstatus(text, sizeof(text));
			tray.update(lamp, text);
			next = now;
			next.tv_sec += interval;
		}

		tray.dispatch();

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
 * to light and describe the state in text.
 */
static int
vmstatus(char *text, size_t len)
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
		(void)snprintf(text, len, "vmd not available");
		return LIGHT_RED;
	}
	vmdescribe(&vc, text, len);
	return vmlight(&vc);
}

/*
 * Start the VM manager in its own session.  The intermediate child exits
 * at once, so the manager is not ours to wait for and outlives us.
 */
void
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

static __dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-x] [-c console] [-i interval]\n",
	    getprogname());
	exit(1);
}
