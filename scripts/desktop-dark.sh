#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
unset AJAXREMOTE_DESKTOP_LIGHT_MODE
exec ~/.local/bin/clickable desktop --arch amd64
