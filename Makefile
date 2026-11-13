#	$OpenBSD$

PROG=		vmdtray
SRCS=		vmdtray.c vmstatus.c

X11BASE?=	/usr/X11R6
CPPFLAGS+=	-I${X11BASE}/include
LDADD+=		-L${X11BASE}/lib -lX11
WARNINGS=	Yes

BINDIR?=	/usr/local/bin
MANDIR?=	/usr/local/man/man

.include <bsd.prog.mk>
