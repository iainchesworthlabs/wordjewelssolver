#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Render every screen to screenshots/<name>.png at 2x with the Windows Edge (headless).
# usage: source/render_all.sh [name ...]   (no names: all)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
WINUSER=$(cmd.exe /c "echo %USERNAME%" 2>/dev/null | tr -d '\r')
WIN_TMP=/mnt/c/Users/$WINUSER/AppData/Local/Temp/wjs-design
EDGE="/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
python3 "$HERE/source/make_mockups.py" | tail -n +2 > "$HERE/source/sizes.txt"
mkdir -p "$WIN_TMP/shots" "$HERE/screenshots"
find "$WIN_TMP" -mindepth 1 -maxdepth 1 ! -name shots -exec rm -rf {} +
(cd "$HERE" && tar --exclude=./screenshots -cf - .) | tar -xmf - --no-same-permissions --no-same-owner -C "$WIN_TMP" 2>/dev/null || true
while read -r name w h; do
  if [ "$#" -gt 0 ] && [[ " $* " != *" $name "* ]]; then continue; fi
  rm -f "$WIN_TMP/shots/$name.png"
  for attempt in 1 2 3; do
    "$EDGE" --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=2 --window-size=$w,$h \
      --virtual-time-budget=4000 --screenshot="C:\\Users\\$WINUSER\\AppData\\Local\\Temp\\wjs-design\\shots\\$name.png" \
      "file:///C:/Users/$WINUSER/AppData/Local/Temp/wjs-design/screens/$name.html#bare" </dev/null >/dev/null 2>&1 || true
    [ -s "$WIN_TMP/shots/$name.png" ] && break
    sleep 2
  done
  if [ -s "$WIN_TMP/shots/$name.png" ]; then cp "$WIN_TMP/shots/$name.png" "$HERE/screenshots/$name.png" && echo "ok $name"; else echo "FAILED $name"; fi
done < "$HERE/source/sizes.txt"
