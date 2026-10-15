#!/bin/bash
# Launch your customized Okular (warm-dark library UI).
# Usage: ./run-okular.sh [optional/file/to/open.pdf]
# Your host home is visible inside the app at /host-home (file dialog).
docker rm -f okular-run1 >/dev/null 2>&1
docker run -d --name okular-run1 \
  -e WAYLAND_DISPLAY=wayland-0 -e XDG_RUNTIME_DIR=/tmp -e HOME=/root \
  -e PATH=/app/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  -e XDG_DATA_DIRS=/app/share:/usr/local/share:/usr/share \
  -e XDG_CONFIG_DIRS=/app/etc/xdg:/etc/xdg \
  -e QT_PLUGIN_PATH=/app/lib64/plugins \
  -v /run/user/1000/wayland-0:/tmp/wayland-0 \
  -v "$HOME/okular-demo/data:/data:ro" \
  -v "$HOME/okular-demo/home:/root" \
  -v "$HOME/focus-demo/app:/app" \
  -v "$HOME:/host-home" \
  okular-custom:lib1 /app/bin/focus "$@"
# Shortcut so the file dialog opens your files in one click (home folder).
docker exec okular-run1 ln -sfn /host-home /root/Host-Files
sleep 3
docker inspect okular-run1 --format "status: {{.State.Status}}"
docker logs okular-run1 2>&1 | head -5
