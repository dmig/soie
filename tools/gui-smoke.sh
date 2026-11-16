#!/bin/sh
#
# gui-smoke.sh - click and type through a browser front on its own Xvfb
# and check that it is still alive after each gesture. It is what a
# person does in a minute of browsing: links, empty space, text
# selection, every kind of field, a select, the middle button, keys
# with nothing selected, a page change while a field has the focus.
# A front that crashes, or an ASan build that reports an error, fails.
#
# Usage: tools/gui-smoke.sh [-d :N] [-v] [FRONT...]
# FRONT: a browser that takes a URL as argument; by default
# browser/xsoie and gtk/gsoie of this tree. asan-build/browser/xsoie
# and asan-build/gtk/gsoie run the same under ASan, /usr/bin/toile the
# installed Toile. -d picks the display (default: the first free one
# from :60), -v prints each step. Needs Xvfb and xdotool; GTK fronts
# run inside dbus-run-session when it is there.
#
# Copyright (C) 2026 Christophe Lincoln <pankso@slitaz.org>
# Written with the assistance of Claude (Anthropic) and Opencode.
#
# SPDX-License-Identifier: BSD-2-Clause
#

cd "$(dirname "$0")/.." || exit 1
DPY=
VERBOSE=
while [ $# -gt 0 ]; do
	case $1 in
	-d) DPY=$2; shift 2 ;;
	-v) VERBOSE=1; shift ;;
	-*) sed -n '/^# Usage/,/^# run/p' "$0"; exit 1 ;;
	*) break ;;
	esac
done
[ $# -gt 0 ] || set -- browser/xsoie gtk/gsoie
for t in Xvfb xdotool; do
	command -v $t >/dev/null || { echo "gui-smoke: $t is missing"; exit 1; }
done
if [ -z "$DPY" ]; then
	n=60
	while [ -e /tmp/.X11-unix/X$n ] || [ -e /tmp/.X$n-lock ]; do
		n=$((n + 1))
	done
	DPY=:$n
fi
TMP=$(mktemp -d) || exit 1
PAGE=file://$PWD/tests/shell/smoke.html

Xvfb $DPY -nolisten tcp -screen 0 1280x1200x24 >/dev/null 2>&1 &
XPID=$!
trap 'kill $XPID 2>/dev/null; wait $XPID; rm -rf "$TMP"' EXIT INT TERM
export DISPLAY=$DPY
i=0
while ! xdotool getmouselocation >/dev/null 2>&1; do
	i=$((i + 1))
	[ $i -gt 50 ] && { echo "gui-smoke: Xvfb $DPY did not start"; exit 1; }
	xdotool sleep 0.1
done

# The gestures, one per line: a name, then xdotool commands (";"
# separates them). Window coordinates: the bands of smoke.html are
# 150 px tall, a click at y = band * 150 + 100 lands in the band under
# a toolbar up to 60 px. Band 0 text, 1 input, 2 textarea, 3 password,
# 4 select, 5 link, 6 empty.
cat > "$TMP/steps" <<'EOF'
empty click|mousemove -w W 400 1000 click 1
link click|mousemove -w W 400 850 click 1
key without selection|key ctrl+c; key ctrl+x; key ctrl+a; key ctrl+c
empty click|mousemove -w W 400 1000 click 1
double click text|mousemove -w W 120 100 click --repeat 2 --delay 80 1
triple click text|mousemove -w W 120 100 click --repeat 3 --delay 80 1
drag text|mousemove -w W 20 70 mousedown 1 mousemove -w W 300 120 mousemove -w W 600 130 mouseup 1
copy page text|key ctrl+c
middle click empty|mousemove -w W 400 1000 click 2
input click|mousemove -w W 300 250 click 1
input type|type --delay 20 hello
input select|key shift+Home ctrl+c ctrl+x ctrl+v ctrl+Left ctrl+shift+Right BackSpace
input drag|mousemove -w W 30 250 mousedown 1 mousemove -w W 400 250 mouseup 1
input double click|mousemove -w W 60 250 click --repeat 2 --delay 80 1
input to empty|mousemove -w W 400 1000 click 1
textarea click|mousemove -w W 300 400 click 1
textarea type|type --delay 20 more; key Return Up Down ctrl+shift+Left BackSpace ctrl+a ctrl+c
textarea triple click|mousemove -w W 60 400 click --repeat 3 --delay 80 1
middle click textarea|mousemove -w W 300 400 click 2
password|mousemove -w W 300 550 click 1; type pw; key ctrl+a ctrl+c ctrl+x
select open|mousemove -w W 300 700 click 1
select key|key Down Escape
select pick|mousemove -w W 300 700 click 1; key Down Return
focused field, link|mousemove -w W 300 250 click 1; mousemove -w W 400 850 click 1
after the page change|key ctrl+c; type x; mousemove -w W 300 250 click 1
back|key alt+Left
link drag|mousemove -w W 100 850 mousedown 1 mousemove -w W 500 870 mouseup 1
EOF

# run FRONT: 0 when it went through every step alive
run() {
	front=$1
	log=$TMP/log
	: > "$log"
	case $(basename "$front") in
	*gsoie*|toile*) wrap=$(command -v dbus-run-session) ;;
	*) wrap= ;;
	esac
	[ -n "$wrap" ] && wrap="$wrap --"
	$wrap "$front" "$PAGE" >"$log" 2>&1 &
	top=$!
	win=
	i=0
	while [ -z "$win" ] && [ $i -lt 100 ]; do
		xdotool sleep 0.1
		win=$(xdotool search --onlyvisible --name "Soie smoke" 2>/dev/null |
			head -1)
		i=$((i + 1))
	done
	# the front itself, under dbus-run-session: its child
	pid=$top
	[ -n "$wrap" ] && pid=$(pgrep -P $top -f "$(basename "$front")" |
		head -1)
	if [ -z "$win" ] || [ -z "$pid" ]; then
		echo "FAIL $front: no window"
		kill $pid $top 2>/dev/null
		return 1
	fi
	xdotool windowmove $win 0 0 windowsize $win 1000 1150
	sleep 1
	fail=
	while IFS='|' read -r name cmds; do
		[ -n "$VERBOSE" ] && echo "  $name"
		echo "$cmds" | tr ';' '\n' | while read -r c; do
			# W: the front's window
			xdotool $(echo "$c" | sed "s/ -w W / --window $win /g")
			xdotool sleep 0.3
		done
		xdotool sleep 0.5
		if ! kill -0 $pid 2>/dev/null; then
			fail="died at \"$name\""
			break
		fi
		if grep -q "AddressSanitizer\|crashed: signal" "$log"; then
			fail="error at \"$name\""
			break
		fi
	done < "$TMP/steps"
	kill $pid 2>/dev/null
	wait $top 2>/dev/null
	if grep -q "AddressSanitizer\|crashed: signal" "$log" && [ -z "$fail" ]
	then
		fail="error on exit"
	fi
	if [ -n "$fail" ]; then
		echo "FAIL $front: $fail"
		grep -A15 "ERROR: AddressSanitizer\|crashed: signal" "$log" |
			head -30
		return 1
	fi
	echo "ok   $front"
}

status=0
for f in "$@"; do
	run "$f" || status=1
done
exit $status
