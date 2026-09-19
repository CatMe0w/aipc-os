#!/usr/bin/env bash
#
# Collects the rows of an SP 800-90B restart test.
#
# One row per power cycle: cut the port, restore it, wait for the stub to
# enumerate, read the result block, keep it if the magic and the sample count
# check out. A thousand rows at roughly twenty seconds each is several hours,
# so run it under tmux and let it resume.
#
# Resuming is why every row is its own file: a row that already exists is
# skipped, so the script can be killed and restarted without losing the matrix
# or repeating a power cycle. A cycle is the expensive operation here, not
# time, so the script never spends one it does not have to.
#
#   tmux new -s restart -d 'baremetal/probes/rng/payload/restart-collect.sh'
#   tmux attach -t restart
#
set -u

ROWS=${ROWS:-1000}
OUTDIR=${OUTDIR:-$HOME/aipc-restart}
HUB=${HUB:-1-3}                 # uhubctl -l; the USB2 side of the VL820
PORT=${PORT:-2}                 # uhubctl -p
OFF_SECONDS=${OFF_SECONDS:-3}
BOOT_TIMEOUT=${BOOT_TIMEOUT:-40}
DEV=${DEV:-/dev/aipc-gdbstub}   # from 99-aipc-gdbstub.rules
# Debian ships gdb-multiarch, not arm-none-eabi-gdb.
GDB=${GDB:-gdb-multiarch}
# uhubctl lives in /usr/sbin and needs root to switch port power.
UHUBCTL=${UHUBCTL:-sudo /usr/sbin/uhubctl}
DUMP_LO=0x32008000
DUMP_HI=0x32009000
MAGIC=52535431                  # RST1, little endian in the file
MAX_CONSECUTIVE_FAIL=${MAX_CONSECUTIVE_FAIL:-10}

mkdir -p "$OUTDIR/rows"
LOG="$OUTDIR/collect.log"

say() { printf '%s %s\n' "$(date -Is)" "$*" | tee -a "$LOG"; }

power() {
	$UHUBCTL -l "$HUB" -p "$PORT" -a "$1" >>"$LOG" 2>&1
}

wait_for_dev() {
	local deadline=$((SECONDS + BOOT_TIMEOUT))
	while [ $SECONDS -lt $deadline ]; do
		[ -e "$DEV" ] && sleep 1 && return 0
		sleep 0.5
	done
	return 1
}

# A row is good only if the payload wrote its magic last and the count matches.
# Anything else means the boot was torn and the row must not enter the matrix.
check_row() {
	[ -s "$1" ] || return 1
	local got
	got=$(od -An -tx4 -N4 "$1" | tr -d ' \n')
	[ "$got" = "$MAGIC" ] || return 1
	got=$(od -An -tu4 -j8 -N4 "$1" | tr -d ' \n')
	[ "$got" = "1000" ]
}

say "start rows=$ROWS outdir=$OUTDIR hub=$HUB port=$PORT dev=$DEV"
fails=0
for i in $(seq -w 1 "$ROWS"); do
	out="$OUTDIR/rows/$i.bin"
	if [ -f "$out" ] && check_row "$out"; then
		continue
	fi

	# Clobber the magic before cutting power. A hub that reports ppps but only
	# disables the port leaves the board running, and the previous boot's
	# result block would then be collected again and again as if it were a
	# thousand fresh rows. Requiring the payload to write the magic back is
	# the only proof that a real reboot happened.
	timeout 60 "$GDB" -nx -batch \
		-ex 'set confirm off' \
		-ex 'set architecture arm' \
		-ex 'set remotetimeout 30' \
		-ex "target remote $DEV" \
		-ex 'set *(unsigned int *)0x32008000 = 0' \
		-ex 'detach' >>"$LOG" 2>&1

	power off
	sleep "$OFF_SECONDS"
	power on

	if ! wait_for_dev; then
		say "row $i: no device after ${BOOT_TIMEOUT}s"
		fails=$((fails + 1))
		[ $fails -ge $MAX_CONSECUTIVE_FAIL ] && { say "giving up after $fails in a row"; exit 1; }
		continue
	fi

	timeout 60 "$GDB" -nx -batch \
		-ex 'set confirm off' \
		-ex 'set architecture arm' \
		-ex 'set remotetimeout 30' \
		-ex "target remote $DEV" \
		-ex "dump binary memory $out $DUMP_LO $DUMP_HI" \
		-ex 'detach' >>"$LOG" 2>&1

	if check_row "$out"; then
		fails=0
		say "row $i ok"
	else
		rm -f "$out"
		say "row $i: bad or missing result block"
		fails=$((fails + 1))
		[ $fails -ge $MAX_CONSECUTIVE_FAIL ] && { say "giving up after $fails in a row"; exit 1; }
	fi
done

say "collected $(ls "$OUTDIR/rows" | wc -l) rows"
