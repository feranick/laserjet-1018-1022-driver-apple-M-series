#!/bin/sh
# -------------------------------------------------------------------
# lj1018.sh - HP LaserJet 1018/1020 on an Asuswrt-Merlin router
#
# Uploads the printer firmware once every time the printer appears on
# the router's USB port (router boot or printer power-on), so the Mac
# can print over the network with zjsFirmware=Never.
# Optionally starts p910nd (Entware) as a raw print server on port 9101.
#
# Copyright (c) 2026 Nicola Ferralis - SPDX-License-Identifier: MIT
#
# Usage:  /jffs/scripts/lj1018.sh [firmware-file] &
#         (start it from /jffs/scripts/services-start)
# -------------------------------------------------------------------

FW=${1:-/jffs/sihp1018.dl}     # sihp1018.dl, sihp1020.dl or hp1020.acl-style .dl
DEV=/dev/usb/lp0
FLAG=/tmp/.lj1018_fw           # /tmp is cleared at every router boot

if [ ! -f "$FW" ]; then
    logger -t lj1018 "firmware file $FW not found"
    exit 1
fi

# Optional raw print server on port 9101 (only if Entware's p910nd is
# installed; Entware may be mounted a while after boot, so wait for it).
( i=0
  while [ $i -lt 60 ] && [ ! -x /opt/sbin/p910nd ]; do i=$((i+1)); sleep 10; done
  [ -x /opt/sbin/p910nd ] && /opt/sbin/p910nd -b -f $DEV 1 ) &

while true; do
    if [ -c $DEV ]; then
        if [ ! -f $FLAG ]; then
            sleep 3                    # let the printer finish booting
            if cat "$FW" > $DEV; then
                touch $FLAG
                logger -t lj1018 "firmware uploaded ($FW)"
            fi
        fi
    else
        rm -f $FLAG                    # printer switched off or unplugged
    fi
    sleep 5
done
