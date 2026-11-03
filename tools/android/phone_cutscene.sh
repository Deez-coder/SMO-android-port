#!/bin/sh
# Start the game on the attached phone, press A on the first-start menu (the on-screen A button), let the opening
# cutscene run, then pull the log and print the sound gaps and frame rates of that stretch.
#   tools/android/phone_cutscene.sh <out folder> [KEY=VALUE ...]      (ANDROID_SERIAL picks the device)
# Needs a save without progress (the menu "Start / Start in Assist Mode") and a warm shader cache for fair numbers.
set -e
OUT=$1; shift
export MSYS_NO_PATHCONV=1
ADB=${ADB:-$(dirname "$0")/../../../toolchain/sdk/platform-tools/adb.exe}
F=/sdcard/Android/data/com.mobileodyssey.game/files
mkdir -p "$OUT"
"$ADB" shell am force-stop com.mobileodyssey.game
printf '%s\n' "$@" > "$OUT/env.txt"
"$ADB" push "$OUT/env.txt" $F/config/env.txt > /dev/null
"$ADB" shell am start -n com.mobileodyssey.game/.GameActivity > /dev/null
sleep ${MENU_WAIT:-45}
SIZE=$("$ADB" shell wm size | sed 's/.*: //' | tr -d '\r')       # natural orientation: <short>x<long>
W=${SIZE#*x}; H=${SIZE%x*}
"$ADB" shell input swipe $((W * 93 / 100)) $((H * 70 / 100)) $((W * 93 / 100)) $((H * 70 / 100)) 300
sleep ${SCENE_WAIT:-60}
"$ADB" exec-out screencap -p > "$OUT/screen.png"
"$ADB" pull $F/log.txt "$OUT/log.txt" > /dev/null
"$ADB" shell rm $F/config/env.txt
"$ADB" shell am force-stop com.mobileodyssey.game
grep "smo_audio: last 2 s" "$OUT/log.txt" | sed 's/smo_audio: last 2 s: //; s/ (5 ms.*//' | tail -15
grep "nvn_vk: frames [0-9.]*:" "$OUT/log.txt" | tail -4
