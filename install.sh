#!/usr/bin/env bash
# Install or update frameeyeosc as a user service on a Steam Frame. Run it on the headset as the
# normal user (no sudo needed):
#   ./install.sh                     install or update, then start
#   ./install.sh --uninstall         remove, keeping settings and learned calibration
#   ./install.sh --uninstall --purge remove everything
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
bin_dir="$HOME/.local/bin"
config_dir="$config_home/frameeyeosc"
unit_dir="$config_home/systemd/user"
unit="frameeyeosc.service"

if [[ "${1:-}" == "--uninstall" ]]; then
    systemctl --user disable --now "$unit" 2>/dev/null || true
    rm -f "$unit_dir/$unit" "$bin_dir/frameeyeosc"
    systemctl --user daemon-reload
    if [[ "${2:-}" == "--purge" ]]; then
        rm -rf "$config_dir"
        echo "Removed frameeyeosc, its settings and its learned calibration."
    else
        echo "Removed frameeyeosc. Settings and calibration are kept in $config_dir (add --purge to delete them)."
    fi
    exit 0
fi

if [[ "$(uname -m)" != "aarch64" ]]; then
    echo "This build is for the Steam Frame (aarch64), but this machine is $(uname -m)." >&2
    exit 1
fi
if [[ ! -e /dev/shm/eye-server.mmap ]]; then
    echo "Note: /dev/shm/eye-server.mmap does not exist yet. It appears once SteamVR's eye tracking runs; the service retries until then."
fi

install -Dm755 "$here/frameeyeosc" "$bin_dir/frameeyeosc"
install -Dm644 "$here/frameeyeosc.service" "$unit_dir/$unit"
if [[ ! -f "$config_dir/env" ]]; then
    install -Dm644 "$here/frameeyeosc.env.example" "$config_dir/env"
fi
systemctl --user daemon-reload
systemctl --user enable "$unit"
systemctl --user restart "$unit"
sleep 2
systemctl --user --no-pager status "$unit" | head -n 5 || true

cat <<EOF

frameeyeosc is installed and starts together with SteamVR.
  Settings: $config_dir/env   (apply with: systemctl --user restart frameeyeosc)
  Logs:     journalctl --user -u frameeyeosc -f

On your PC, turn off Steam Link's own OSC output (SteamVR settings > Steam Link > OSC),
otherwise it drives the avatar's eyes too with unsmoothed data.
EOF
