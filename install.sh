#!/bin/bash
# hpzjs installer - native driver for HP LaserJet 1018 / 1020 / 1022
# Copyright (c) 2026 Nicola Ferralis - SPDX-License-Identifier: MIT
#
# Usage:  sudo ./install.sh [path/to/hp1020.acl]
#
# Works from the source tree (builds first) or from a binary release
# (uses the prebuilt files in build/).
#
# Installs:
#   /Library/Printers/hpzjs/filter/{rastertozjs,commandtozjs}
#   /Library/Printers/hpzjs/firmware/hp1020.acl   (copied from HP's driver)
#   /Library/Printers/PPDs/Contents/Resources/HP-LaserJet-10xx-hpzjs.ppd
set -e

PREFIX=/Library/Printers/hpzjs
PPDDIR=/Library/Printers/PPDs/Contents/Resources
HPRES=/Library/Printers/hp/laserjet/hplaserjetzjs.bundle/Contents/Resources
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ "$(id -u)" != "0" ]; then
    echo "Please run with sudo: sudo $0 $*" >&2
    exit 1
fi
cd "$HERE"

# ---- build (source tree) or use prebuilt binaries (release) ----------------
if [ -f Makefile ] && [ -d src ]; then
    if ! file build/rastertozjs 2>/dev/null | grep -q Mach-O; then
        rm -rf build
    fi
    if ! xcode-select -p >/dev/null 2>&1; then
        echo "The Xcode Command Line Tools are required: xcode-select --install" >&2
        exit 1
    fi
    echo "==> Building"
    if [ -n "$SUDO_USER" ]; then
        sudo -u "$SUDO_USER" make
    else
        make
    fi
fi
for f in build/rastertozjs build/commandtozjs; do
    if ! file "$f" 2>/dev/null | grep -q Mach-O; then
        echo "Missing or invalid $f" >&2
        exit 1
    fi
done
ls build/ppd/*.ppd >/dev/null

echo "==> Installing filters in $PREFIX/filter"
install -d -o root -g wheel -m 755 "$PREFIX" "$PREFIX/filter" \
    "$PREFIX/firmware" "$PREFIX/icons"
install -o root -g wheel -m 755 build/rastertozjs build/commandtozjs \
    "$PREFIX/filter/"

# ---- firmware (needed by the 1018 and 1020 after every power-on) ----------
FW=""
for f in "$1" "$HPRES/hp1020.acl" "$PREFIX/firmware/hp1020.acl"; do
    if [ -n "$f" ] && [ -f "$f" ]; then FW="$f"; break; fi
done
if [ "$FW" = "$PREFIX/firmware/hp1020.acl" ]; then
    echo "==> Keeping installed firmware $FW"
elif [ -n "$FW" ]; then
    echo "==> Installing firmware from $FW"
    install -o root -g wheel -m 644 "$FW" "$PREFIX/firmware/hp1020.acl"
else
    echo "WARNING: hp1020.acl not found. The LaserJet 1018/1020 need it after"
    echo "         every power-on. It is part of HP's hplaserjetzjs.bundle"
    echo "         (Contents/Resources/hp1020.acl). Re-run:"
    echo "             sudo $0 /path/to/hp1020.acl"
fi

# ---- icon (optional, from HP's driver if installed) -----------------------
if [ -f "$HPRES/HP LaserJet 1022.icns" ]; then
    install -o root -g wheel -m 644 "$HPRES/HP LaserJet 1022.icns" \
        "$PREFIX/icons/HP LaserJet.icns"
fi

echo "==> Installing PPDs in $PPDDIR"
install -d -m 755 "$PPDDIR"
install -o root -g wheel -m 644 build/ppd/*.ppd "$PPDDIR/"

# Let cupsd pick up the new drivers
launchctl kickstart -k system/org.cups.cupsd 2>/dev/null || true

cat <<EOF

Done. Next steps:

  * New printer: System Settings > Printers & Scanners > Add Printer,
    select the LaserJet, then in "Use:" choose "Select Software..." and
    pick "HP LaserJet 1018 hpzjs" (or 1020 / 1022).

  * Or switch an existing queue to the new driver:
      sudo lpadmin -p QUEUE_NAME -P "$PPDDIR/HP-LaserJet-1018-hpzjs.ppd"

    Existing queues on this Mac:
EOF
lpstat -v 2>/dev/null | sed 's/^/      /' || true
echo
