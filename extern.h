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

/* The lamps of the traffic light, from top to bottom. */
enum light { LIGHT_RED, LIGHT_YELLOW, LIGHT_GREEN, LIGHT_MAX };

/* What "vmctl status" reported. */
struct vmcount {
	int	total;		/* VMs listed */
	int	up;		/* VMs running */
};

/*
 * A system tray protocol, XEmbed or StatusNotifierItem.  The main loop
 * polls fd(), calls dispatch() when it is readable and update() with the
 * lamp to light and a description whenever the state is checked.
 */
struct tray {
	int	(*fd)(void);
	void	(*dispatch)(void);
	void	(*update)(int, const char *);
};

/* Colors as 0xrrggbb. */
extern const unsigned int colorhousing;
extern const unsigned int colorlit[LIGHT_MAX];
extern const unsigned int colorunlit[LIGHT_MAX];

__BEGIN_DECLS
void	 iconrender(unsigned char *, int, int);
void	 launch(void);
int	 sni_init(struct tray *);
void	 vmdescribe(const struct vmcount *, char *, size_t);
int	 vmlight(const struct vmcount *);
int	 vmparse(FILE *, struct vmcount *);
int	 xembed_init(struct tray *);
__END_DECLS
