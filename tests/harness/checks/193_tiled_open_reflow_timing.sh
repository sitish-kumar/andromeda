#!/usr/bin/env bash
# A tiled opener and every surviving tile share geometry progress with windows_in. A shorter windows_move timeline
# must not finish the reflow while the opening effect is still near its beginning.
set -euo pipefail

readonly IMAGE="$UMBRIEL_RUNTIME_DIR/tiled-open-reflow.png"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[colors]
backdrop = "#000000FF"

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

[layout]
mode = "master"

[animation.windows_in]
enabled = true
duration_ms = 1600
curve = "linear"
style = "fade"

[animation.windows_out]
enabled = false

[animation.windows_move]
enabled = true
duration_ms = 150
curve = "linear"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  local title=$1 color=$2
  FILL_COLOR="$color" "$UMBRIEL_UNMAP_CLIENT" "$title" 1200 700 > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
  for _ in $(seq 80); do
    if "$UMBRIEL" windows --json | jq -e --arg title "$title" '.[] | select(.title == $title)' > /dev/null; then
      return 0
    fi
    sleep 0.025
  done
  echo "timed out waiting for $title"
  return 1
}

# Only the survivor is red. Its pixel count on a middle row directly observes its presented width without relying on
# IPC target geometry or the opening window's opacity.
red_width() {
  grim "$IMAGE"
  magick "$IMAGE" -alpha off -crop '1280x8+0+356' +repage \
    -fx '(r > 0.8 && g < 0.1 && b < 0.1) ? 1 : 0' -format '%[fx:round(w*mean)]\n' info:
}

spawn tiled-open-survivor 0xFFFF0000
sleep 1.7
before=$(red_width)

spawn tiled-opener 0xFF0000FF
sleep 0.35
early=$(red_width)

sleep 1.4
final=$(red_width)

if ((before - final < 300)); then
  echo "opening a second tile did not produce a measurable reflow: before=$before final=$final"
  exit 1
fi
if ! ((early > final + 150 && early < before - 50)); then
  echo "tiled opening reflow did not follow windows_in: before=$before early=$early final=$final"
  exit 1
fi

echo "tiled opening reflow followed windows_in beyond its shorter windows_move timeline"
