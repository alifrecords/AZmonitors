#!/bin/bash
# AZ Monitoring — Azimxodjayev Music
# VST3 (Cubase, Ableton, FL Studio, Studio One, Reaper), AU (Logic Pro, GarageBand)
# va AAX (Pro Tools, agar paketda bo'lsa) ni o'rnatadi.
set -e
cd "$(dirname "$0")"

echo "AZ Monitoring o'rnatilmoqda..."
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"

cp -R "AZ Monitoring.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
echo "  VST3 -> ~/Library/Audio/Plug-Ins/VST3"

if [ -d "AZ Monitoring.component" ]; then
  cp -R "AZ Monitoring.component" "$HOME/Library/Audio/Plug-Ins/Components/"
  echo "  AU   -> ~/Library/Audio/Plug-Ins/Components"
fi

if [ -d "AZ Monitoring.aaxplugin" ]; then
  sudo mkdir -p "/Library/Application Support/Avid/Audio/Plug-Ins"
  sudo cp -R "AZ Monitoring.aaxplugin" "/Library/Application Support/Avid/Audio/Plug-Ins/"
  echo "  AAX  -> /Library/Application Support/Avid/Audio/Plug-Ins"
fi

# Imzolanmagan build uchun: macOS karantin belgisini olib tashlash
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/AZ Monitoring.vst3" 2>/dev/null || true
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/Components/AZ Monitoring.component" 2>/dev/null || true

# Logic AU keshini yangilash
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Tayyor. DAW'ni qayta ishga tushiring (Logic: Plug-in Manager -> Reset & Rescan)."
