#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

static int compare_detection_peak_desc(const void* a, const void* b) {
    const sbss_detection* da = (const sbss_detection*)a;
    const sbss_detection* db = (const sbss_detection*)b;

    if (da->peak < db->peak) {
        return 1;
    }
    if (da->peak > db->peak) {
        return -1;
    }
    return 0;
}

static void compute_stats(const double* pixels, long count, double* mean, double* sigma) {
    long i;
    double sum = 0.0;
    double sum_sq = 0.0;

    for (i = 0; i < count; ++i) {
        sum += pixels[i];
        sum_sq += pixels[i] * pixels[i];
    }

    *mean = sum / (double)count;
    {
        double variance = (sum_sq / (double)count) - ((*mean) * (*mean));
        if (variance < 1e-20) {
            variance = 1e-20;
        }
        *sigma = sqrt(variance);
    }
}

static int is_local_maximum(
    const double* pixels,
    long width,
    long height,
    long x,
    long y,
    int radius,
    double threshold
) {
    long yy;
    long xx;
    const double center = pixels[y * width + x];

    if (center <= threshold) {
        return 0;
    }

    for (yy = y - radius; yy <= y + radius; ++yy) {
        for (xx = x - radius; xx <= x + radius; ++xx) {
            if (xx < 0 || yy < 0 || xx >= width || yy >= height) {
                continue;
            }
            if (xx == x && yy == y) {
                continue;
            }
            if (pixels[yy * width + xx] >= center) {
                return 0;
            }
        }
    }

    return 1;
}

static int estimate_area(
    const double* pixels,
    long width,
    long height,
    long x,
    long y,
    int radius,
    double threshold
) {
    int area = 0;
    long yy;
    long xx;

    for (yy = y - radius; yy <= y + radius; ++yy) {
        for (xx = x - radius; xx <= x + radius; ++xx) {
            if (xx < 0 || yy < 0 || xx >= width || yy >= height) {
                continue;
            }
            if (pixels[yy * width + xx] > threshold) {
                area++;
            }
        }
    }

    return area;
}

static void compute_centroid(
    const double* pixels,
    long width,
    long height,
    long x,
    long y,
    int radius,
    double threshold,
    double* cx,
    double* cy,
    double* flux
) {
    long yy;
    long xx;
    double weighted_x = 0.0;
    double weighted_y = 0.0;
    double weighted_sum = 0.0;

    for (yy = y - radius; yy <= y + radius; ++yy) {
        for (xx = x - radius; xx <= x + radius; ++xx) {
            double value;
            if (xx < 0 || yy < 0 || xx >= width || yy >= height) {
                continue;
            }

            value = pixels[yy * width + xx] - threshold;
            if (value <= 0.0) {
                continue;
            }

            weighted_sum += value;
            weighted_x += value * (double)xx;
            weighted_y += value * (double)yy;
        }
    }

    if (weighted_sum <= 0.0) {
        *cx = (double)x;
        *cy = (double)y;
        *flux = pixels[y * width + x];
        return;
    }

    *cx = weighted_x / weighted_sum;
    *cy = weighted_y / weighted_sum;
    *flux = weighted_sum;
}

int sbss_detect_from_fits(
    const char* fits_path,
    const sbss_config* cfg,
    sbss_detection* detections,
    size_t detections_capacity,
    size_t* detection_count,
    char* error_message,
    size_t error_message_size
) {
    static double pixel_workspace[SBSS_MAX_IMAGE_PIXELS];
    sbss_config local_cfg;
    long width = 0;
    long height = 0;
    long x;
    long y;
    long n_pix;
    double mean = 0.0;
    double sigma = 0.0;
    double threshold = 0.0;
    size_t out_count = 0;
    int radius = 0;

    if (detections == NULL || detection_count == NULL || fits_path == NULL) {
        sbss_set_error(error_message, error_message_size, "invalid detection input");
        return -1;
    }

    if (cfg != NULL) {
        local_cfg = *cfg;
    } else {
        sbss_config_set_defaults(&local_cfg);
    }

    if (local_cfg.filter_size < 1 || (local_cfg.filter_size % 2) == 0) {
        sbss_set_error(error_message, error_message_size, "FILTER_SIZE must be an odd positive value");
        return -1;
    }

    if (local_cfg.detect_minarea < 1) {
        sbss_set_error(error_message, error_message_size, "DETECT_MINAREA must be >= 1");
        return -1;
    }

    if (local_cfg.max_sources < 1) {
        sbss_set_error(error_message, error_message_size, "MAX_SOURCES must be >= 1");
        return -1;
    }

    radius = local_cfg.filter_size / 2;

    if ((size_t)local_cfg.max_sources > detections_capacity) {
        sbss_set_error(
            error_message,
            error_message_size,
            "MAX_SOURCES exceeds provided detection capacity (%d > %zu)",
            local_cfg.max_sources,
            detections_capacity
        );
        return -1;
    }

    if (sbss_read_fits_image(
            fits_path,
            pixel_workspace,
            SBSS_MAX_IMAGE_PIXELS,
            &width,
            &height,
            error_message,
            error_message_size
        ) != 0) {
        return -1;
    }

    n_pix = width * height;
    if (n_pix <= 0) {
        sbss_set_error(error_message, error_message_size, "empty FITS image");
        return -1;
    }

    compute_stats(pixel_workspace, n_pix, &mean, &sigma);
    threshold = mean + (local_cfg.detect_thresh_sigma * sigma);

    (void)memset(detections, 0, detections_capacity * sizeof(sbss_detection));

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            int area;
            double cx;
            double cy;
            double flux;
            const double peak = pixel_workspace[y * width + x];

            if (out_count >= (size_t)local_cfg.max_sources) {
                break;
            }

            if (!is_local_maximum(pixel_workspace, width, height, x, y, radius, threshold)) {
                continue;
            }

            area = estimate_area(pixel_workspace, width, height, x, y, radius, threshold);
            if (area < local_cfg.detect_minarea) {
                continue;
            }

            compute_centroid(
                pixel_workspace,
                width,
                height,
                x,
                y,
                radius,
                threshold,
                &cx,
                &cy,
                &flux
            );

            detections[out_count].x = cx;
            detections[out_count].y = cy;
            detections[out_count].peak = peak;
            detections[out_count].flux = flux;
            detections[out_count].area = area;
            out_count++;
        }

        if (out_count >= (size_t)local_cfg.max_sources) {
            break;
        }
    }

    qsort(detections, out_count, sizeof(sbss_detection), compare_detection_peak_desc);
    *detection_count = out_count;
    return 0;
}
