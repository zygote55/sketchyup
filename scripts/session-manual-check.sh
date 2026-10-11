#!/usr/bin/env bash
# Owner-driven checks with a real mouse and keyboard: N03 pointer capture and
# focus (pointer) and R085 input methods (input-method). The script starts an
# owned SketchyUp with private settings, prints the checklist, records window
# screenshots on request and stores the verdict. It changes no desktop settings.
# See docs/M9_HARDWARE_SESSION.md, sections 6 and 7.
set -euo pipefail
if (( $# != 2 )) || [[ $1 != pointer && $1 != input-method ]] || [[ $2 != wayland && $2 != xcb ]]; then
  echo 'Usage: session-manual-check.sh pointer|input-method wayland|xcb' >&2
  echo '  input-method honours SESSION_IM_MODULE (default fcitx; or wayland, compose).' >&2
  exit 2
fi
check=$1
platform=$2
source "$(dirname -- "${BASH_SOURCE[0]}")/session-lib.sh"

if [[ $check == pointer ]]; then
  checklist=$(cat <<'EOF'
N03 pointer capture and focus (real mouse; both outputs connected):
 P1 Push/Pull (P): press on the slab top, drag up past the window edge onto the other
    output, release there. Expect: one completed or cancelled push/pull, no stuck drag,
    the tool returns to its idle state; Undo lists at most one entry.
 P2 Same as P1, releasing over another application's window. Then click back in
    SketchyUp: the first click is handled normally (not swallowed or treated as a drag end).
 P3 Line (L): click a start point, move outside the window and back, click to finish,
    Escape. Expect: the rubber band follows on re-entry; no stray segment.
 P4 Orbit with the middle button: drag out of the window and release outside. Moving
    back in does not keep orbiting.
 P5 Select (Space): window-drag a selection rectangle out of the window and release
    outside. Expect: the selection matches the rectangle or is cancelled; no stuck band.
 P6 Keyboard focus through tool changes: press L, R, P, M, Space; then click tool rail
    buttons. After each change, type 1000 and Enter: digits go to Measurements, the
    focus ring is visible, and F6 / Shift+F6 still cycle regions.
 P7 Repeat P1 with the window on the other output (mixed scale 2 and 1.6).
EOF
)
else
  im=${SESSION_IM_MODULE:-fcitx}
  [[ $im =~ ^(fcitx|wayland|compose|ibus)$ ]] || session_die 'Unsupported SESSION_IM_MODULE'
  session_app_env=(QT_IM_MODULE="$im")
  pgrep -x fcitx5 >/dev/null || [[ $im != fcitx ]] || session_die 'fcitx5 is not running.'
  checklist=$(cat <<EOF
R085 input methods (QT_IM_MODULE=$im, $platform). Switch input methods with fcitx5's hotkey.
 I1 Dead keys (keyboard-us-intl in fcitx5): in Measurements with the Line tool active,
    type 2' then 6" (dead ' and dead ", each followed by a digit or space). Expect the
    imperial value is accepted as typed, or rejected with a clear message; never a
    silently different length.
 I2 Dead keys in text fields: rename an Outliner entity (F2) to "Café crème" and a tag
    to "Ñandú". Expect exact text after Enter, in the Outliner and after save/reopen.
 I3 Compose (if a Compose key is configured): type Compose o / to get ø in a text field.
 I4 IME (for example Mozc or Pinyin): in a text field, compose a word and see the
    preedit underline inside the field; commit it. Expect the committed text only, no
    duplicate or lost characters, and Escape cancels the preedit, not the dialog.
 I5 IME active in Measurements: type 2500. Expect either ASCII digits accepted or a
    clear rejection (full-width digits); no crash. Switch the IME off and retry.
 I6 Shortcuts with the IME on: press L, P, Ctrl+Z in the viewport. Expect tools switch
    and the IME does not swallow the keys (or note exactly which keys it takes).
EOF
)
  im_profile=${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5/profile
  im_info=$(
    echo "QT_IM_MODULE=$im XMODIFIERS=${XMODIFIERS:-} GTK_IM_MODULE=${GTK_IM_MODULE:-}"
    pacman -Q fcitx5 fcitx5-qt fcitx5-gtk fcitx5-mozc fcitx5-chinese-addons fcitx5-configtool ibus 2>&1 || true
    if [[ -f $im_profile ]]; then cat "$im_profile"; fi
  )
fi

printf '%s\n' "$checklist"
session_confirm "Start SketchyUp ($platform) for the $check check?" || exit 1
session_init "$check-$platform"
if [[ $check == input-method ]]; then printf '%s\n' "$im_info" > "$session_evidence/input-methods.txt"; fi
printf '%s\n' "$checklist" > "$session_evidence/checklist.txt"
session_monitors before
session_app_start "$platform" --demo
trap session_app_stop EXIT
session_wait_window 30
echo 'Type a step id (for example P1) and Enter to take a screenshot and note; Enter alone to finish.'
while :; do
  read -r -p 'step> ' step
  [[ -z $step ]] && break
  [[ $step =~ ^[A-Za-z0-9-]{1,12}$ ]] || { echo 'Use a short step id.'; continue; }
  read -r -p "  observation for $step: " observation
  session_note "$step: $observation"
  session_screenshot "step-$step-$(date +%H%M%S)"
done
session_app_alive && session_note 'app alive at end' || session_note 'APP NOT RUNNING at end'
session_result
