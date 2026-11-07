#!/bin/bash
# Ectolens brings a Spectacle user's settings along once, and never touches them again.
# usage: tests/e2e/settings-copy.sh BUILD_BIN_DIR
set -u
BIN=$1; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/.config"; printf '[GuiConfig]\nquitAfterSaveCopyExport=true\n' > "$T/.config/spectaclerc"
run() { HOME=$T XDG_CONFIG_HOME=$T/.config QT_QPA_PLATFORM=offscreen timeout 10 "$BIN/ectolens" --version >/dev/null 2>&1; }
run
grep -q "quitAfterSaveCopyExport=true" "$T/.config/ectolensrc" 2>/dev/null || { echo "FAIL  Spectacle's settings were not copied"; exit 1; }
printf '[GuiConfig]\nquitAfterSaveCopyExport=false\n' > "$T/.config/spectaclerc"
run
grep -q "quitAfterSaveCopyExport=true" "$T/.config/ectolensrc" || { echo "FAIL  a later Spectacle change overwrote Ectolens's own settings"; exit 1; }
echo "PASS  settings copied once from spectaclerc"
# Spectacle's "copy file location" choice becomes After Capture's Copy to clipboard › File Path
T2=$(mktemp -d); trap 'rm -rf "$T" "$T2"' EXIT
mkdir -p "$T2/.config"; printf '[General]\nclipboardGroup=PostScreenshotCopyLocation\n' > "$T2/.config/spectaclerc"
HOME=$T2 XDG_CONFIG_HOME=$T2/.config QT_QPA_PLATFORM=offscreen timeout 10 "$BIN/ectolens" --version >/dev/null 2>&1
grep -q "afterCaptureCopyWhat=CopyTheFilePath" "$T2/.config/ectolensrc" && ! grep -q "clipboardGroup" "$T2/.config/ectolensrc" \
    || { echo "FAIL  Spectacle's copy choice did not carry over to After Capture"; cat "$T2/.config/ectolensrc"; exit 1; }
echo "PASS  Spectacle's copy choice carried over to After Capture"
