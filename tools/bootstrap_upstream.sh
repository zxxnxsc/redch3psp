#!/usr/bin/env bash
set -euo pipefail

UPSTREAM_URL="https://github.com/SugaryHull/re3.git"
UPSTREAM_COMMIT="31dacfe8edb01ca2aae3069e7c777c4849cf5adc"
TARGET_DIR="${1:-upstream/re3}"

if [ -e "$TARGET_DIR" ]; then
  echo "Target already exists: $TARGET_DIR" >&2
  exit 1
fi

mkdir -p "$(dirname "$TARGET_DIR")"
git clone "$UPSTREAM_URL" "$TARGET_DIR"
git -C "$TARGET_DIR" checkout "$UPSTREAM_COMMIT"

echo "Pinned upstream re3 at $UPSTREAM_COMMIT"
echo "Next: apply the PSP reconstruction patches from this repository."
