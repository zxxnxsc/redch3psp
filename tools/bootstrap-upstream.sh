#!/usr/bin/env bash
set -euo pipefail

UPSTREAM="https://github.com/SugaryHull/re3.git"
UPSTREAM_COMMIT="31dacfe8edb01ca2aae3069e7c777c4849cf5adc"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$ROOT/work/re3"

rm -rf "$WORK"
mkdir -p "$(dirname "$WORK")"

echo "[redch3psp] cloning pinned re3 upstream"
git clone --quiet "$UPSTREAM" "$WORK"
git -C "$WORK" checkout --quiet "$UPSTREAM_COMMIT"

for patch in "$ROOT"/patches/*.patch; do
  [ -e "$patch" ] || continue
  echo "[redch3psp] checking $(basename "$patch")"
  git -C "$WORK" apply --check "$patch"
  echo "[redch3psp] applying $(basename "$patch")"
  git -C "$WORK" apply "$patch"
done

echo "[redch3psp] reconstruction tree ready: $WORK"
echo "[redch3psp] upstream commit: $UPSTREAM_COMMIT"
