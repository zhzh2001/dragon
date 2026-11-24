#!/bin/sh
# The Pi's half of tools/release/check_package.sh, run on the Pi over ssh.
#
#   sh check_package_pi.sh VERSION
#
# Unpacks ~/Dragon-VERSION-pi.tar.gz into ~/dragon-check-VERSION and runs
# the packaged build headless at 640x480: the valley, the start screen, a
# hoard run from the start screen, 900 frames of the demo pilot (the number
# docs/porting/MOBILE.md quotes), and an arena fight. No reference frames:
# the Pi runs the fixed-function tier, and there is no set for it.
# The demo pilot's 900 frames fail under PI_FPS_FLOOR (30; the 0.4.0 package
# ran at 38.8).
# A Pi 1B takes about four minutes.
v=$1
d=$HOME/dragon-check-$v
rm -rf "$d"; mkdir -p "$d"
# The Pi has no clock of its own: tar warns of every file's future time.
tar xzf "$HOME/Dragon-$v-pi.tar.gz" -C "$d" 2> /dev/null
cd "$d/Dragon" || { echo "FAIL the package did not unpack"; exit 0; }

run() { # name, args...
  name=$1; shift
  /usr/bin/time -f 'wall %e s, peak %M KB' ./dragon --headless --size 640x480 "$@" --screenshot "$d/$name.bmp" > "$d/$name.log" 2>&1
}
frames() { grep -m1 -o 'frames: .*' "$d/$1.log"; }
cost() { grep -m1 -o 'wall .*' "$d/$1.log"; }

run valley --frames 60
if [ -s "$d/valley.bmp" ]; then echo "PASS the valley: $(frames valley); $(cost valley)"; else echo "FAIL the valley rendered nothing: $(tail -1 "$d/valley.log")"; fi

run menu --menu --frames 60
if [ -s "$d/menu.bmp" ]; then echo "PASS the start screen: $(cost menu)"; else echo "FAIL the start screen rendered nothing"; fi

run start --menu-start 2 --frames 300 --hide-panels
if grep -q "run: seed" "$d/start.log" && [ -s "$d/start.bmp" ]; then echo "PASS a hoard run from the start screen: $(frames start)"; else echo "FAIL no run started from the start screen"; fi

DRAGON_FRAMETIMES=$d/frametimes.csv run run7 --run 7 --autopilot --frames 900 --hide-panels
fps=$(frames run7 | sed -E 's/.* ([0-9.]+) fps.*/\1/')
floor=${PI_FPS_FLOOR:-30}
if [ -s "$d/run7.bmp" ] && awk "BEGIN{exit !(${fps:-0} >= $floor)}"; then echo "PASS run 7, the demo pilot: $(frames run7); $(cost run7)"
elif [ -s "$d/run7.bmp" ]; then echo "FAIL run 7, the demo pilot, ran under $floor fps: $(frames run7)"; else echo "FAIL run 7 did not finish: $(tail -1 "$d/run7.log")"; fi

run arena --bots 3 --match --bot-range 95 --attack --frames 400 --hide-panels
if [ -s "$d/arena.bmp" ]; then echo "PASS the arena, three bots: $(frames arena)"; else echo "FAIL the arena did not finish: $(tail -1 "$d/arena.log")"; fi

echo "executable $(sha256sum dragon | cut -c1-64), $(vcgencmd measure_temp 2> /dev/null)"
