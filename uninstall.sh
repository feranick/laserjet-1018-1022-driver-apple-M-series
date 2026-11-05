#!/bin/bash
# hpzjs uninstaller
# Copyright (c) 2026 Nicola Ferralis - SPDX-License-Identifier: MIT
set -e
if [ "$(id -u)" != "0" ]; then
    echo "Please run with sudo: sudo $0" >&2
    exit 1
fi
rm -rf /Library/Printers/hpzjs
rm -f /Library/Printers/PPDs/Contents/Resources/HP-LaserJet-10*-hpzjs.ppd
launchctl kickstart -k system/org.cups.cupsd 2>/dev/null || true
echo "hpzjs removed. Printer queues that used it must be re-created."
