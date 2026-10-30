#!/usr/bin/env bash
set -euo pipefail
# Linux generates an empty manifest: its runtime remains a system dependency.
cmake "-DMANIFEST=$1/compiler-runtime-release.txt" "-DOUTPUT=$2" \
  -P "$(dirname "$0")/copy-compiler-runtime.cmake"
