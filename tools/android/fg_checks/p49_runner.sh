#!/bin/sh
# p49: as p48, but the test ini becomes the game's own graphics.ini (the in-game menu edits that file), the user's graphics.ini kept in $BAK and put back at the end. usage: RUNS=..., INI=<local ini>
S="${SMO_TEST_DIR:-$PWD/fg_out}"      # where the runs' logs and backups go
A="${ADB:-adb}"      # adb (add -s <serial> with several phones)
F=/sdcard/Android/data/com.mobileodyssey.game/files
export MSYS_NO_PATHCONV=1 ADB=$A      # ANDROID_SERIAL=<serial> picks a phone when several are attached
BAK="$S/bak47"
if [ -n "$($A shell pidof com.mobileodyssey.game:game)" ]; then echo the game is running on the phone, stopping here; exit 1; fi
mkdir -p "$BAK"
for f in Common.bin File1.bin; do $A pull $F/userdata/save/$f "$BAK/$f" > /dev/null 2>&1 || exit 1; done && { $A pull $F/config/graphics.ini "$BAK/graphics.ini" > /dev/null 2>&1 || exit 1; }
$A shell "input keyevent KEYCODE_WAKEUP"; sleep 2
if $A shell dumpsys window | grep -q 'isKeyguardShowing=true'; then echo the phone is locked, unlock it first; exit 1; fi
cool() { i=0; while [ $i -lt 80 ]; do t=$($A shell "dumpsys battery" | grep -m1 temperature | tr -dc '0-9'); s=$($A shell "dumpsys thermalservice | grep -m1 'Thermal Status'" | tr -dc '0-9'); if [ "${t:-999}" -le ${COOL_T:-340} ] && [ "${s:-9}" -le 0 ]; then echo "cool: battery $t"; return; fi; i=$((i + 1)); sleep 15; done; echo "not cool: $t $s"; }
cd "$(dirname "$0")/../../.."      # the repository
run() {
  n=$1; secs=$2; shift 2
  cool
  rm -rf "$S/$n"; mkdir -p "$S/$n"
  ( sleep $((secs - 12)); $A exec-out screencap -p < /dev/null > "$S/$n/shot.png" 2>/dev/null ) &
  sh tools/android/run_shots.sh "$S/$n" $secs "$@" SMO_FUNCTIONS=../functions/main_functions.tsv SMO_START_STAGE=${STAGE:-WaterfallWorldHomeStage} SMO_SHOT_EVERY=100000 > /dev/null 2>&1
  wait
  echo "--- $n done: $*"
}
[ -n "$INI" ] && $A push "$INI" $F/config/graphics.ini > /dev/null
while IFS= read -r line; do [ -n "$line" ] || continue; eval "run $line" < /dev/null; done <<EOT
$RUNS
EOT
$A shell "am force-stop com.mobileodyssey.game; rm -f $F/config/env.txt $F/config/graphics_test.ini"
for f in Common.bin File1.bin; do $A push "$BAK/$f" $F/userdata/save/$f > /dev/null 2>&1; done
$A push "$BAK/graphics.ini" $F/config/graphics.ini > /dev/null 2>&1 && echo "graphics.ini restored"
echo tests finished, save restored
