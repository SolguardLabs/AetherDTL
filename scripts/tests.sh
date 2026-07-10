#!/usr/bin/env bash
set -euo pipefail

NODE_BIN="${NODE_BIN:-node}"
if ! command -v "$NODE_BIN" >/dev/null 2>&1; then
  if command -v node.exe >/dev/null 2>&1; then
    NODE_BIN="node.exe"
  elif [ -x "/c/Program Files/nodejs/node.exe" ]; then
    NODE_BIN="/c/Program Files/nodejs/node.exe"
  fi
fi

"$NODE_BIN" scripts/build.mjs
"$NODE_BIN" --test "tests/node/*.test.ts"
