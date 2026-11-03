#!/usr/bin/env bash
# Regenerates the TextMate grammar and refreshes the copies bundled into each
# editor package, so every editor ships the same index and highlighting.
set -euo pipefail
cd "$(dirname "$0")"
python3 grammar/build.py
{ echo "// NOTE: copy of editors/lsp/grav-index.js, bundled so the packaged .vsix is self-contained (editors/sync.sh)."
  cat lsp/grav-index.js; } > vscode/grav-index.js
echo "synced vscode/grav-index.js"
