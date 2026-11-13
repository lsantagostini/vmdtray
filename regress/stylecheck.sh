#!/bin/sh
#	$OpenBSD$
#
# Check C sources for the mechanical parts of style(9): the RCS id on the
# first line, no line longer than 80 columns with 8 column tabs, no
# trailing whitespace, no space before a tab and no indentation made of
# spaces instead of tabs.  Print every offending line and exit 1 if any.

status=0
for f in "$@"; do
	if [ "$(sed -n 1p "$f")" != "/*	\$OpenBSD\$	*/" ]; then
		echo "$f:1: missing RCS id"
		status=1
	fi
	expand "$f" | awk -v f="$f" '
		length > 80 { print f ":" NR ": longer than 80 columns"; s = 1 }
		END { exit s }' || status=1
	awk -v f="$f" '
		/[ \t]+$/ { print f ":" NR ": trailing whitespace"; s = 1 }
		/ \t/ { print f ":" NR ": space before tab"; s = 1 }
		/^\t*        / { print f ":" NR ": spaces instead of tab"; s = 1 }
		END { exit s }' "$f" || status=1
done
exit $status
