#!/bin/sh
# Start the game on the attached phone, press A on the title screen (continue the save), play nothing for a while
# (gameplay idles in Cap Kingdom), then pull the log and print the driver, the device and the frame rates.
#   tools/android/phone_play.sh <out folder> [KEY=VALUE ...]      (ANDROID_SERIAL picks the device; TITLE_WAIT, PLAY_WAIT seconds)
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
sleep ${TITLE_WAIT:-85}
"$ADB" exec-out screencap -p > "$OUT/title.png"
SIZE=$("$ADB" shell wm size | sed 's/.*: //' | tr -d '\r'); W=${SIZE#*x}; H=${SIZE%x*}
"$ADB" shell input swipe $((W * 93 / 100)) $((H * 70 / 100)) $((W * 93 / 100)) $((H * 70 / 100)) 300
sleep 6                                             # a second press: the first is sometimes taken while the menu is still settling
"$ADB" shell input swipe $((W * 93 / 100)) $((H * 70 / 100)) $((W * 93 / 100)) $((H * 70 / 100)) 300
sleep ${PLAY_WAIT:-60}
"$ADB" exec-out screencap -p > "$OUT/play.png"
"$ADB" pull $F/log.txt "$OUT/log.txt" > /dev/null
"$ADB" shell rm $F/config/env.txt
grep -E "custom Vulkan driver|SMO_DRIVER|nvn_vk: device 0|extensions of the design|driver support for pipelines|nvn_vk_path|HOST EXC|FATAL|REFUSED" "$OUT/log.txt" | cut -c1-260 | head -12
grep "nvn_vk: frames [0-9.]*:" "$OUT/log.txt" | tail -4
