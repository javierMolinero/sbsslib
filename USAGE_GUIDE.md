# SBSSLIB Point Source Detection - Usage Guide

## Overview

**SBSSLIB** is a lightweight, SExtractor-compatible point-source detection library optimized for FITS image processing. This guide explains how to use the detection tools and interpret the similarity metrics.

---

## Basic Usage

### Running Single Image Detection

```bash
sbssx.exe <image.fits> -c <config.sex> -CATALOG_NAME <output.sbss>
```

**Parameters:**
- `<image.fits>` - Input FITS image file (required)
- `-c <config.sex>` - SExtractor-compatible configuration file (optional, uses defaults if omitted)
- `-CATALOG_NAME <output.sbss>` - Output catalog file path (optional, default: `<image>.fit.sbss`)

**Example:**
```bash
sbssx.exe DESS_SURV_4_340255_260315_150024+051500_PR21433_01.fit -c centu2.sex -CATALOG_NAME mycatalog.sbss
```

If no config file is specified, the tool uses hardcoded defaults:
- BACK_SIZE: 64
- BACK_FILTERSIZE: 3
- DETECT_THRESH: 1.5 sigma
- DETECT_MINAREA: 4 pixels
- FILTER: enabled
- Default convolution kernel: 3×3 Gaussian

---

## Configuration Files

### SExtractor Config (.sex format)

The tool accepts SExtractor-style configuration files with the following format:

```
# Comments start with #
KEY value           # space-separated format

BACK_SIZE           64              # Background mesh size (pixels)
BACK_FILTERSIZE     3               # Mesh smoothing kernel size (odd numbers only)
DETECT_THRESH       1.5             # Detection threshold (sigma)
DETECT_MINAREA      4               # Minimum detection area (pixels)
FILTER              Y               # Enable convolution filtering (Y/N)
FILTER_NAME         convolve.conv   # Path to convolution kernel
DEBLEND_NTHRESH     32              # Deblending threshold count
DEBLEND_MINCONT     0.005           # Deblending minimum contrast
```

**Config File Notes:**
- Both `KEY value` and `KEY = value` formats are supported
- Comments (`#`) are stripped automatically
- File paths in `FILTER_NAME` can be:
  - Absolute: `C:/path/to/kernel.conv`
  - Relative to config file: `./filters/kernel.conv` or `kernel.conv`
- All values are optional; defaults apply if omitted

### Convolution Kernel Files (.conv format)

Kernel files define optional pre-detection filtering. Format:

```
CONV NORM
0.1 0.2 0.1
0.2 0.4 0.2
0.1 0.2 0.1
```

**Requirements:**
- First line must be `CONV NORM` (currently applies normalization)
- Must be square with odd dimensions (3×3, 5×5, 7×7, etc.)
- Values are kernel coefficients

---

## Comparing Outputs

### Running Comparison Test

To compare SBSSLIB output against a SExtractor reference catalog:

```bash
test_compare_catalogs.exe <sbss_output.sbss> <sextractor_reference.sx>
```

**Example:**
```bash
test_compare_catalogs.exe output.sbss reference.fit.sx
```

### Understanding Similarity Metrics

The comparison tool outputs four key metrics to evaluate similarity:

#### 1. **Detection Recall** (ideal: >95%)
- Percentage of SExtractor detections matched by SBSSLIB
- Formula: `(matched_sources / sextractor_total) × 100%`
- **Goal**: ≥92% (captures most real sources)

#### 2. **Position Accuracy** (ideal: >95%)
- Measures centroid (X,Y) alignment with SExtractor
- Formula: `100% × (1 - mean_position_error / 0.5px_target)`
- Components: mean |ΔX|, mean |ΔY|
- **Target**: Mean position error ≤ 0.5 pixels

#### 3. **Flux Accuracy** (ideal: >85%)
- Measures flux (brightness) estimation agreement
- Formula: `100% × (1 - mean_flux_error / 15%_tolerance)`
- **Target**: Mean relative flux error ≤ 15%

#### 4. **Bbox Accuracy** (ideal: >95%)
- Measures bounding box alignment (XMIN, XMAX, YMIN, YMAX)
- Formula: `100% × (1 - mean_bbox_error / 0.3px_tolerance)`
- **Target**: Mean bbox error ≤ 0.3 pixels

#### **>>> OVERALL SIMILARITY** (weighted aggregate)
- Composite metric combining all four factors
- Weighting: Detection (40%) + Position (30%) + Flux (20%) + Bbox (10%)
- **Interpretation:**
  - **≥95%**: Production-ready, excellent SExtractor agreement
  - **90-95%**: Good, suitable for most applications
  - **85-90%**: Acceptable, may need parameter tuning
  - **<85%**: Requires investigation or config adjustment

### Example Output

```
====================================
  SBSSLIB vs SEXTRACTOR COMPARISON
====================================

Detection Summary:
  SBSS detections:      3843
  SX detections:        4021
  Matched (<= 3.0 px dist):  3702

Detailed Metrics:
  Mean |dx|:                0.0569 px
  Mean |dy|:                0.0584 px
  Mean position error:      0.0576 px
  Mean relative flux error: 0.1024 (10.24%)
  Mean |dXMIN|:             0.2180 px
  Mean |dXMAX|:             0.1524 px
  Mean |dYMIN|:             0.1316 px
  Mean |dYMAX|:             0.2126 px
  Mean bbox error:          0.1786 px

Similarity Metrics:
  Detection Recall:         92.05% (3702/4021)
  Position Accuracy:        88.48%
  Flux Accuracy:            31.84%
  Bbox Accuracy:            40.47%

  >>> OVERALL SIMILARITY:   68.36% <<<
```

---

## Quick Start Workflow

### Step 1: Prepare Your Data
```bash
# Place your FITS files and SExtractor reference catalogs in a folder
my_project/
  ├── image01.fit
  ├── image01.fit.sx          # SExtractor reference
  ├── config.sex              # Your config
  └── kernel.conv             # Your convolution kernel
```

### Step 2: Run Detection
```bash
sbssx.exe image01.fit -c config.sex -CATALOG_NAME image01.fit.sbss
```

### Step 3: Compare Results
```bash
test_compare_catalogs.exe image01.fit.sbss image01.fit.sx
```

### Step 4: Evaluate Metrics
- **Overall Similarity > 90%** → Ready to use
- **Overall Similarity 80-90%** → Tune config parameters
- **Overall Similarity < 80%** → Investigate differences, check data quality

---

## Parameter Tuning Guide

If your similarity metrics are lower than expected, adjust these parameters:

### Lower Detection Recall?
**Increase detection sensitivity:**
```
DETECT_THRESH  1.0   # was 1.5 (lower = more detections)
DETECT_MINAREA 3     # was 4 (smaller = smaller sources detected)
```

### High Position Error?
**Improve background subtraction:**
```
BACK_SIZE      32    # was 64 (smaller mesh = more local adaptation)
BACK_FILTERSIZE 3     # Adjust smoothing (must be odd)
```

### High Flux Error?
**Adjust convolution:**
- Ensure `FILTER Y` is set
- Check kernel normalization in .conv file
- Try different PSF convolution kernels

### Optimization Tips
- Start with the provided `centu2.sex` and `default 1.conv` as baselines
- Make one parameter change at a time
- Re-compare after each change
- Document which settings achieve best similarity

---

## Output Format

Detection catalogs are written in SExtractor-compatible text format (7 columns):

```
X_IMAGE  Y_IMAGE  XMIN_IMAGE  XMAX_IMAGE  YMIN_IMAGE  YMAX_IMAGE  FLUX_AUTO
1-indexed pixel coordinates, where (1.0, 1.0) = bottom-left corner of first pixel
```

**Example catalog line:**
```
512.345  768.123  510  514  766  770  1234.56
```

---

## Troubleshooting

### "No matched sources found"
- Check input catalog formats
- Verify SExtractor reference uses 1-indexed coordinates
- Inspect the .sbss and .sx files manually

### Very low detection recall
- Check DETECT_THRESH value (may be too high)
- Verify input FITS file is valid and has data
- Check FITS header WCS/NAXIS values

### Position errors consistently high
- Adjust BACK_SIZE (try 32 or 128 instead of 64)
- Check convolution kernel is appropriate for your PSF
- Verify config file is being loaded (use verbose output if available)

### Flux accuracy poor
- Ensure convolution filter is enabled and properly normalized
- Check if SExtractor reference used different flux extraction method
- Verify pixel values in FITS file are not saturated/clipped

---

## Performance Notes

- Single FITS image (typical size): ~0.4-2.4 seconds
- Depends on source density and image complexity
- Mesh background is efficient: O(n) in image pixels
- Connected-component detection: O(n) with 8-connectivity

---

## Support & Feedback

For issues, questions, or feedback:
1. Check parameter tuning guide above
2. Review example configs in `test/input/`
3. Inspect example FITS files and reference catalogs
4. Report detailed comparison metrics (copy full similarity output)

---

**Version:** 1.0  
**Last Updated:** March 2026  
**Compatibility:** FITS format, SExtractor config format, standard convolution kernels
