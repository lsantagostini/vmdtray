#	$OpenBSD$

PROG=		vmdtray
SRCS=		vmdtray.c icon.c sni.c vmstatus.c xembed.c

X11BASE?=	/usr/X11R6
DBUS_CFLAGS!=	pkg-config --cflags dbus-1
DBUS_LIBS!=	pkg-config --libs dbus-1

CPPFLAGS+=	-I${X11BASE}/include ${DBUS_CFLAGS}
LDADD+=		-L${X11BASE}/lib -lX11 ${DBUS_LIBS}
WARNINGS=	Yes

BINDIR?=	/usr/local/bin
MANDIR?=	/usr/local/man/man

.include <bsd.prog.mk>
