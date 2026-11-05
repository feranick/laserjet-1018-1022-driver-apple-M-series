# Printing through an Asuswrt-Merlin router

These steps let the Mac print wirelessly to a LaserJet 1018 or 1020 that
is plugged into the USB port of an ASUS router running Asuswrt-Merlin.
They were tested on an RT-AC68U with a LaserJet 1018.

Two things happen:

- **Router.** `lj1018.sh` uploads the printer's firmware every time the
  printer appears on the USB port, both at router boot and after the
  printer is switched off and on. It uploads only once per power-on;
  a 1018 that already runs its firmware does not accept a second copy,
  and the upload then blocks the USB port.
- **Mac.** The driver (`rastertozjs`) produces the print data and sends
  it to the router's print server, which passes it to the printer
  unchanged.

## Router setup

1. In the router's web interface, go to **Administration → System** and
   set **Enable JFFS custom scripts and configs** to *Yes*.
2. Copy the firmware and the script to the router from the Mac. Adjust
   the user name and address.
   ```sh
   ssh admin@192.168.1.1 'cat > /jffs/sihp1018.dl' < sihp1018.dl
   ssh admin@192.168.1.1 'cat > /jffs/scripts/lj1018.sh' < contrib/asuswrt-merlin/lj1018.sh
   ```
3. On the router, make the script start at boot:
   ```sh
   chmod +x /jffs/scripts/lj1018.sh
   echo '/jffs/scripts/lj1018.sh /jffs/sihp1018.dl &' >> /jffs/scripts/services-start
   chmod +x /jffs/scripts/services-start
   ```
   If you already have another script that sends the firmware at boot,
   remove it: the printer must not receive the firmware twice.
4. Reboot the router, or switch the printer off and on and run
   `/jffs/scripts/lj1018.sh /jffs/sihp1018.dl &`. Then check:
   ```sh
   grep lj1018 /tmp/syslog.log      # "firmware uploaded"
   ls -l /tmp/.lj1018_fw            # flag file present
   ```

## Mac setup

Install the driver (see the main README). Then point the printer at the
router's raw print server and turn the driver's own firmware upload off:

```sh
sudo lpadmin -p HP1018 -E -v "socket://192.168.1.1:9100?waiteof=false" \
  -P /Library/Printers/PPDs/Contents/Resources/HP-LaserJet-1018-hpzjs.ppd \
  -o zjsFirmware=Never
```

`waiteof=false` matters: the ASUS print server never closes the
connection, so without it every job stays at "Waiting for printer to
finish".

### If the router's own print server does not work

Install Entware (`amtm` → Entware, needs a USB drive), then:

```sh
opkg install p910nd
```

`lj1018.sh` then also starts `p910nd` on port **9101**, which forwards
raw data both ways. Use `socket://192.168.1.1:9101?waiteof=false` as the
printer address.

## Troubleshooting

- **The printer ignores jobs.** Check `ps | grep "cat " | grep -v grep`
  on the router. A `cat` that never exits means a firmware upload is
  blocked. Run `killall cat`, switch the printer off and on, and let
  the script upload the firmware again.
- **`sh: can't create /dev/usb/lp0: Device or resource busy`.** Another
  process has the USB port open, usually a stuck `cat` as above. To
  find it:
  ```sh
  for p in /proc/[0-9]*; do ls -l $p/fd 2>/dev/null | grep -q usb/lp && echo "$(basename $p): $(tr '\0' ' ' < $p/cmdline)"; done
  ```
- **Testing without CUPS.** You can send a print file straight to the
  printer over SSH:
  ```sh
  ssh admin@192.168.1.1 'cat > /dev/usb/lp0' < job.zjs
  ```
