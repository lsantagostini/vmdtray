#	$OpenBSD$

PROG=		t_vmstatus
SRCS=		t_vmstatus.c vmstatus.c
NOMAN=		Yes
WARNINGS=	Yes
CPPFLAGS+=	-I${.CURDIR}/..
.PATH:		${.CURDIR}/..

REGRESS_TARGETS=	run-regress-${PROG} run-regress-style \
			run-regress-manual

run-regress-style:
	sh ${.CURDIR}/stylecheck.sh ${.CURDIR}/../*.[ch] ${.CURDIR}/*.c

run-regress-manual:
	mandoc -Tlint -Wstyle ${.CURDIR}/../vmdtray.1

.include <bsd.regress.mk>
