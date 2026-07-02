#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
~/.local/bin/clickable build --arch arm64
exec ~/.local/bin/clickable install --arch arm64
