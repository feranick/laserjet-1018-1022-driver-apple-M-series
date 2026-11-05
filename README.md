# hpzjs — native macOS driver for HP LaserJet 1018 / 1020 / 1022

A replacement for HP's `hplaserjetzjs.bundle` (v1.6.1). HP's filters,
`rasterToHPZJS` and `commandToHPZJS`, exist only as x86_64 binaries and
need Rosetta. This driver is plain C. It builds as a universal binary
(Apple Silicon + Intel) and links only against the system `libcups`.

Version: 2026.11.05.1 · License: MIT

## Requirements

- macOS 11 (Big Sur) or later, on Apple Silicon or Intel.
- To compile: the Xcode Command Line Tools.
  ```sh
  xcode-select --install
  ```
- For the **LaserJet 1018 and 1020**: the firmware file `hp1020.acl`.
  These two models keep their firmware in RAM, so the driver uploads it
  after every power-on. The file comes with HP's driver at
  `/Library/Printers/hp/laserjet/hplaserjetzjs.bundle/Contents/Resources/hp1020.acl`
  and is **not** distributed with this project.
  The 1022 has its firmware in ROM and does not need it.

## Compiling

```sh
git clone <this repository>
cd laserjet-1018-1022-driver-apple-M-series
make
```

This produces:

```
build/rastertozjs                         raster filter (universal, ad-hoc signed)
build/commandtozjs                        command filter (cleaning page)
build/ppd/HP-LaserJet-1018-hpzjs.ppd
build/ppd/HP-LaserJet-1020-hpzjs.ppd
build/ppd/HP-LaserJet-1022-hpzjs.ppd
```

Check the result:

```sh
file build/rastertozjs        # Mach-O universal binary with 2 architectures
build/rastertozjs --version
```

Other `make` targets:

| Target | Action |
|---|---|
| `make dist` | builds `dist/hpzjs-<version>-macos.tar.gz`, a binary release with the filters, PPDs, install scripts, README and LICENSE |
| `make install` | same as `sudo ./install.sh` |
| `make uninstall` | same as `sudo ./uninstall.sh` |
| `make clean` | removes `build/` and `dist/` |

To build for a single architecture: `make ARCHS="-arch arm64"`.

## Installing

From the source tree, or from an unpacked binary release:

```sh
sudo ./install.sh
```

If `hp1020.acl` is not at its usual place, give its path:

```sh
sudo ./install.sh /path/to/hp1020.acl
```

The installer:

- builds the driver when it is run from the source tree;
- installs the filters to `/Library/Printers/hpzjs/filter/`;
- installs the firmware to `/Library/Printers/hpzjs/firmware/hp1020.acl`;
- installs the PPDs to `/Library/Printers/PPDs/Contents/Resources/`;
- restarts `cupsd`.

Then set up the printer:

- **New printer:** in System Settings → Printers & Scanners, choose Add
  Printer and select the LaserJet. Under *Use*, choose *Select Software…*
  and pick **HP LaserJet 1018 hpzjs** (or 1020 / 1022).
- **Existing queue:** switch it to the new driver.
  ```sh
  lpstat -v        # list queue names
  sudo lpadmin -p QUEUE_NAME -P /Library/Printers/PPDs/Contents/Resources/HP-LaserJet-1018-hpzjs.ppd
  ```

### Uninstalling

```sh
sudo ./uninstall.sh
```

## Printer options

| Option | Values |
|---|---|
| Resolution | 600 dpi (default), ProRes 1200 dpi |
| Halftoning | Screen (≈106 lpi, 45° clustered dot), Error diffusion, Threshold |
| Mid-tone lightening | None / Light / Medium / Strong (gamma 1.0–1.8, compensates for toner dot gain) |
| Firmware Upload | Automatic (default), Before every job, Never |
| REt, EconoMode, Print Density, Jam Recovery, Media type, Tray / Manual feed | as in HP's driver |

## How it works

The protocol was recovered by reverse engineering HP's `rasterToHPZJS`
1.6.1.

- **Firmware (1018/1020).** Before each job, the filter asks the printer
  for its firmware version through the CUPS back channel (ACL query
  `00 ac 00 01 …`). If the printer's version is older than the `.acl`
  file, the filter sends `@PJL ENTER LANGUAGE=ACL`, then
  `00 ac c0 de <size-8>`, then the file, then UEL, and waits for the
  printer to come back online. If the printer does not answer, the
  filter checks the IEEE-1284 device ID for `FWVER` instead.
- **Job stream.** A job is sent as:
  1. PJL, ending with `@PJL ENTER LANGUAGE = ZJS`;
  2. `JZJZ`;
  3. ZjStream START_DOC;
  4. for each page: START_PAGE (14 items), JBIG_BIH, JBIG_HID chunks
     (≤ 64 KiB, the last one padded to 16 bytes), END_JBIG, END_PAGE;
  5. END_DOC;
  6. PJL end.
- **Compression.** Pages are compressed with JBIG1 (ITU-T T.82): one
  layer, two-line template, TPBON, 128-line stripes, BIH options 0x5c.
- **Page geometry.** Width is padded to 64 pixels and height to 8 lines.
  Paper codes are Windows DMPAPER values, as in HP's driver.

Verification:

- **JBIG encoder:** compared with HP's own encoder, run under x86_64
  emulation. Output was byte-identical on 35 test bitmaps.
- **Complete ZjStream:** compared with HP's code for multi-page jobs at
  600 and 1200 dpi. Output was byte-identical.

The halftone screen is original: HP's halftone tables were not copied.

### Limitations

- HP's "FastRes 1200" mode (2 bits per pixel) is not implemented. Use
  600 dpi or ProRes 1200.
- No toner level reporting (these printers do not provide it).

## Troubleshooting

```sh
sudo cupsctl --debug-logging
tail -f /var/log/cups/error_log | grep -i -E "hpzjs|rastertozjs"
```

Lines starting with `hpzjs firmware:` show the printer's reply to the
firmware version query. Turn debug logging off afterwards with
`sudo cupsctl --no-debug-logging`.

## Source layout

```
Makefile              build, dist, install targets
install.sh            installer (source tree or binary release)
uninstall.sh
ppd/hpzjs.ppd.in      PPD template; make generates the 1018/1020/1022 PPDs
src/rastertozjs.c     CUPS raster filter: PJL + ZjStream writer
src/zjs.h             ZjStream protocol constants
src/jbig_enc.[ch]     JBIG1 (T.82) encoder
src/halftone.[ch]     8-bit gray to 1-bit (screen / error diffusion / threshold)
src/firmware.[ch]     1018/1020 firmware version query and upload
src/commandtozjs.c    CUPS command filter (cleaning page)
```
