#!/usr/bin/env bash
set -euo pipefail

NODE_BIN="${NODE_BIN:-node}"
NPM_BIN="${NPM_BIN:-npm}"
if ! command -v "$NODE_BIN" >/dev/null 2>&1; then
  if command -v node.exe >/dev/null 2>&1; then
    NODE_BIN="node.exe"
  elif [ -x "/c/Program Files/nodejs/node.exe" ]; then
    NODE_BIN="/c/Program Files/nodejs/node.exe"
  fi
fi

if ! command -v "$NPM_BIN" >/dev/null 2>&1; then
  if command -v npm.cmd >/dev/null 2>&1; then
    NPM_BIN="npm.cmd"
  fi
fi

"$NPM_BIN" ci
"$NPM_BIN" run format:check
"$NODE_BIN" scripts/build.mjs --warnings
"$NODE_BIN" --test "tests/node/*.test.ts"
"$NODE_BIN" scripts/smoke.mjs
"$NODE_BIN" scripts/check-loc.mjs
