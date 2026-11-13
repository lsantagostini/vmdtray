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

#include "extern.h"

#define SAMPLES	4		/* per side of a pixel, for antialiasing */

const unsigned int colorhousing = 0x0c0c0c;
const unsigned int colorlit[LIGHT_MAX] = { 0xe53935, 0xfdd835, 0x43a047 };
const unsigned int colorunlit[LIGHT_MAX] = { 0x2a0e0e, 0x2a270e, 0x0e2410 };

static int	 sample(double, double, int, int, unsigned int *);

/*
 * Draw the traffic light with the given lamp lit into a size by size
 * image of 4 bytes per pixel, alpha, red, green and blue in that order
 * (ARGB32 in network byte order, as StatusNotifierItem wants).  Each pixel
 * is the average of SAMPLES x SAMPLES points, so the edges are smooth and
 * the space around the housing is transparent.
 */
void
iconrender(unsigned char *buf, int size, int lamp)
{
	double fx, fy;
	unsigned char *p;
	unsigned int c, n, r, g, b;
	int sx, sy, x, y;

	for (y = 0; y < size; y++) {
		for (x = 0; x < size; x++) {
			n = r = g = b = 0;
			for (sy = 0; sy < SAMPLES; sy++) {
				fy = y + (sy + 0.5) / SAMPLES;
				for (sx = 0; sx < SAMPLES; sx++) {
					fx = x + (sx + 0.5) / SAMPLES;
					if (sample(fx, fy, size, lamp, &c) == 0)
						continue;
					n++;
					r += c >> 16 & 0xff;
					g += c >> 8 & 0xff;
					b += c & 0xff;
				}
			}
			p = buf + 4 * (y * size + x);
			p[0] = n * 0xff / (SAMPLES * SAMPLES);
			p[1] = n == 0 ? 0 : r / n;
			p[2] = n == 0 ? 0 : g / n;
			p[3] = n == 0 ? 0 : b / n;
		}
	}
}

/*
 * Tell the color at point (px, py) of an image of the given size, which
 * the housing or a lamp covers, and return 1; return 0 if the point is
 * transparent.  The geometry matches what the XEmbed window draws: three
 * lamps of diameter d separated by gap, and a housing gap wider around
 * them, all centered.
 */
static int
sample(double px, double py, int size, int lamp, unsigned int *color)
{
	double cx, cy, dx, dy, top;
	int d, gap, i;

	if ((gap = size / 22) < 1)
		gap = 1;
	d = (size - 4 * gap) / 3;
	top = (size - (3 * d + 4 * gap)) / 2.0;
	cx = size / 2.0;

	if (px < cx - d / 2.0 - gap || px > cx + d / 2.0 + gap ||
	    py < top || py > top + 3 * d + 4 * gap)
		return 0;
	for (i = 0; i < LIGHT_MAX; i++) {
		cy = top + gap + i * (d + gap) + d / 2.0;
		dx = px - cx;
		dy = py - cy;
		if (dx * dx + dy * dy <= d * d / 4.0) {
			*color = i == lamp ? colorlit[i] : colorunlit[i];
			return 1;
		}
	}
	*color = colorhousing;
	return 1;
}
