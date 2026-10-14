#!/usr/bin/env bash
set -euo pipefail
exec python3 "$(dirname "$0")/verify.py" "${1:-focused}" --preset "${2:-linux-core}" "${@:3}"
