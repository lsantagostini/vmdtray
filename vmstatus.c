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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"

/*
 * Count the VMs listed in the output of "vmctl status" read from fp, and
 * how many of them are running.  Each VM is a line of nine fields ending
 * in its state and name.  The header is the only line that starts with
 * "ID".  Any other line, such as an error message, is ignored.
 * Return 0, or -1 if reading failed.
 */
int
vmparse(FILE *fp, struct vmcount *vc)
{
	char *field, *line, *name, *p, *state;
	size_t linesize;
	int fields;

	vc->total = vc->up = 0;
	line = NULL;
	linesize = 0;
	while (getline(&line, &linesize, fp) != -1) {
		fields = 0;
		name = state = NULL;
		p = line;
		while ((field = strsep(&p, " \t\r\n")) != NULL) {
			if (*field == '\0')
				continue;
			if (fields++ == 0 && strcmp(field, "ID") == 0)
				break;
			state = name;
			name = field;
		}
		if (fields != 9)
			continue;
		vc->total++;
		if (strcmp(state, "running") == 0)
			vc->up++;
	}
	free(line);
	if (ferror(fp))
		return -1;
	return 0;
}

/*
 * Choose the lamp to light: green when every VM is running, red when none
 * is running or there are none, and yellow in between.
 */
int
vmlight(const struct vmcount *vc)
{
	if (vc->up == 0)
		return LIGHT_RED;
	if (vc->up == vc->total)
		return LIGHT_GREEN;
	return LIGHT_YELLOW;
}
