#!/bin/bash
# hpzjs installer - native driver for HP LaserJet 1018 / 1020 / 1022
# Copyright (c) 2026 Nicola Ferralis - SPDX-License-Identifier: MIT
#
# Usage:  sudo ./install.sh [firmware files...]
#         e.g. sudo ./install.sh ~/firmwares/sihp1018.dl ~/firmwares/sihp1020.dl
#
# Works from the source tree (builds first) or from a binary release
# (uses the prebuilt files in build/).
#
# Installs:
#   /Library/Printers/hpzjs/filter/{rastertozjs,commandtozjs}
#   /Library/Printers/hpzjs/firmware/hp1018.fw, hp1020.fw  (.acl or .dl)
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
# Accepted: HP's hp1020.acl and foo2zjs' sihp1018.dl / sihp1020.dl.
# Files named on the command line win over the default locations. The
# model is taken from the file name (1018 / 1020); a file with neither in
# its name is used for both. Installed as firmware/hp1018.fw, hp1020.fw.
is_firmware() {
    local magic
    magic=$(head -c 12 "$1" 2>/dev/null | LC_ALL=C tr -c '0-9A-Za-z' '.')
    case "$magic" in
        [0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9].ELF) return 0 ;;
    esac
    head -c 32 "$1" 2>/dev/null | LC_ALL=C grep -q "ENTER LANGUAGE=ACL"
}

FW1018=""; FW1020=""
take_firmware() {
    if ! is_firmware "$1"; then
        echo "WARNING: $1 is not an HP firmware file (.acl or .dl), skipped" >&2
        return
    fi
    case "$(basename "$1")" in
        *1018*) [ -z "$FW1018" ] && FW1018="$1" ;;
        *1020*) [ -z "$FW1020" ] && FW1020="$1" ;;
        *)      [ -z "$FW1018" ] && FW1018="$1"
                [ -z "$FW1020" ] && FW1020="$1" ;;
    esac
    return 0
}
for f in "$@"; do
    if [ -f "$f" ]; then
        take_firmware "$f"
    else
        echo "WARNING: $f not found" >&2
    fi
done
for f in "$HPRES/hp1020.acl" \
         /usr/local/share/foo2zjs/firmware/sihp1018.dl \
         /usr/local/share/foo2zjs/firmware/sihp1020.dl \
         /opt/homebrew/share/foo2zjs/firmware/sihp1018.dl \
         /opt/homebrew/share/foo2zjs/firmware/sihp1020.dl
do
    if [ -f "$f" ]; then take_firmware "$f"; fi
done
# Upgrade from earlier versions that installed firmware/hp1020.acl
if [ -z "$FW1020" ] && [ -f "$PREFIX/firmware/hp1020.acl" ]; then
    FW1020="$PREFIX/firmware/hp1020.acl"
fi

for m in 1018 1020; do
    eval src=\$FW$m
    dst="$PREFIX/firmware/hp$m.fw"
    if [ -n "$src" ]; then
        echo "==> Firmware for LaserJet $m: $src"
        install -o root -g wheel -m 644 "$src" "$dst"
    elif [ -f "$dst" ]; then
        echo "==> Keeping installed firmware $dst"
    fi
done
# HP's own Mac driver used the 1020 image for the 1018 as well
if [ ! -f "$PREFIX/firmware/hp1018.fw" ] && [ -f "$PREFIX/firmware/hp1020.fw" ]; then
    echo "==> No 1018-specific firmware given: using the 1020 image for the 1018"
    install -o root -g wheel -m 644 "$PREFIX/firmware/hp1020.fw" \
        "$PREFIX/firmware/hp1018.fw"
fi
rm -f "$PREFIX/firmware/hp1020.acl"
if [ ! -f "$PREFIX/firmware/hp1018.fw" ]; then
    echo "WARNING: no firmware installed. The LaserJet 1018/1020 need it after"
    echo "         every power-on. Re-run with the firmware file(s), e.g.:"
    echo "             sudo $0 /path/to/sihp1018.dl"
    echo "         (HP's hp1020.acl or foo2zjs' sihp1018.dl / sihp1020.dl)"
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
