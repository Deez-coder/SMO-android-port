#!/bin/sh
# Run the installed app with the given env.txt lines, wait, and pull the frame pictures and the log.
#   tools/android/run_shots.sh <out folder> <seconds> 'KEY=VALUE' ...
# Used for A/B tests on the emulator (for example SMO_VK_NO_BC=1 against the GPU's own BC decoding).
set -e
OUT=$1; SECS=$2; shift 2
export MSYS_NO_PATHCONV=1
ADB=${ADB:-$(dirname "$0")/../../../toolchain/sdk/platform-tools/adb.exe}
F=/sdcard/Android/data/com.mobileodyssey.game/files
mkdir -p "$OUT"
"$ADB" shell am force-stop com.mobileodyssey.game
printf '%s\n' "SMO_SHOT_DIR=../shots" "$@" > "$OUT/env.txt"
"$ADB" push "$OUT/env.txt" $F/config/env.txt > /dev/null
"$ADB" shell rm -rf $F/shots
"$ADB" shell mkdir -p $F/shots
"$ADB" shell am start -n com.mobileodyssey.game/.GameActivity > /dev/null
sleep "$SECS"
"$ADB" exec-out screencap -p > "$OUT/screen.png"
"$ADB" shell am force-stop com.mobileodyssey.game
"$ADB" pull $F/shots "$OUT/" > /dev/null
"$ADB" pull $F/log.txt "$OUT/log.txt" > /dev/null
"$ADB" shell rm $F/config/env.txt
ls "$OUT/shots"
