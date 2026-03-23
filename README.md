# sbsslib

`sbsslib` is a compact source-detection library inspired by SExtractor and focused only on point detection from FITS images.

It includes:
- A C library API (`sbsslib`) for FITS point detection.
- A command-line executable (`sbssx`) with SExtractor-like options.
- A text configuration file for detection tuning.

## Features

- FITS image input through CFITSIO.
- Threshold-based local-maxima point detection.
- No dynamic memory allocation in sbsslib detection path.
- Configurable detection parameters:
  - `DETECT_THRESH`
  - `DETECT_MINAREA`
  - `FILTER_SIZE`
  - `MAX_SOURCES`
- SExtractor-like text output catalog with `.sbss` extension.

## Fixed capacities (MISRA-oriented)

The detector uses fixed-size buffers and does not allocate from the heap.

- `SBSS_MAX_IMAGE_PIXELS` default: `4096 * 4096`
- `SBSS_MAX_DETECTIONS` default: `50000`

These can be overridden at compile time, for example:

```bash
cmake -S . -B build -DSBSS_MAX_IMAGE_PIXELS=33554432 -DSBSS_MAX_DETECTIONS=80000
```

## Build (Linux)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Build (Windows, PowerShell)

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Dependency

CFITSIO is required by default.

- Linux: install `cfitsio` development package (`libcfitsio-dev` on Debian/Ubuntu).
- Windows: install CFITSIO and set `CFITSIO_ROOT` to the install prefix containing `include` and `lib`.

Example (for this repository local install):

```powershell
$env:CFITSIO_ROOT = "C:\Users\jamo\Documents\projects\sbsslib\third_party\cfitsio_install"
$env:ZLIB_ROOT = "C:\Users\jamo\Documents\projects\sbsslib\third_party\zlib_install"
cmake -S . -B build
```

## CLI usage

```bash
sbssx image.fits -c config/sbsslib.conf
```

By default, output is written as `image.fits.sbss`.

```bash
sbssx image.fits -c config/sbsslib.conf -CATALOG_NAME catalog.sbss
```

Override selected config keys from CLI:

```bash
sbssx image.fits -c config/sbsslib.conf -DETECT_THRESH 4.0 -DETECT_MINAREA 7
```

## Output catalog

Current output columns (matching `centu2.param` up to `FLUX_AUTO`):
- `X_IMAGE`
- `Y_IMAGE`
- `XMIN_IMAGE`
- `XMAX_IMAGE`
- `YMIN_IMAGE`
- `YMAX_IMAGE`
- `FLUX_AUTO`

## Repository setup for remote

```bash
git init
git add .
git commit -m "Initial sbsslib scaffold"
git branch -M main
git remote add origin <your-remote-url>
git push -u origin main
```
