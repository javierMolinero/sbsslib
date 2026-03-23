#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define SBSS_MIN_BACK_SIZE 8
#define SBSS_MAX_MESH_PER_AXIS ((4096 / SBSS_MIN_BACK_SIZE) + 2)
#define SBSS_MAX_MESH_COUNT (SBSS_MAX_MESH_PER_AXIS * SBSS_MAX_MESH_PER_AXIS)

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

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int apply_background_subtraction(
    double* pixels,
    long width,
    long height,
    int back_size,
    int back_filtersize,
    char* error_message,
    size_t error_message_size
) {
    static double mesh_bg[SBSS_MAX_MESH_COUNT];
    static double mesh_bg_filtered[SBSS_MAX_MESH_COUNT];
    int nx;
    int ny;
    int mesh_count;
    int mx;
    int my;
    long x;
    long y;

    if (back_size < SBSS_MIN_BACK_SIZE) {
        sbss_set_error(error_message, error_message_size, "BACK_SIZE must be >= %d", SBSS_MIN_BACK_SIZE);
        return -1;
    }

    if (back_filtersize < 1) {
        back_filtersize = 1;
    }
    if ((back_filtersize % 2) == 0) {
        back_filtersize += 1;
    }

    nx = (int)((width + back_size - 1) / back_size);
    ny = (int)((height + back_size - 1) / back_size);
    mesh_count = nx * ny;

    if (mesh_count <= 0 || mesh_count > SBSS_MAX_MESH_COUNT) {
        sbss_set_error(error_message, error_message_size, "invalid background mesh geometry");
        return -1;
    }

    for (my = 0; my < ny; ++my) {
        for (mx = 0; mx < nx; ++mx) {
            long x0 = (long)mx * (long)back_size;
            long y0 = (long)my * (long)back_size;
            long x1 = x0 + (long)back_size;
            long y1 = y0 + (long)back_size;
            double sum = 0.0;
            long count = 0;

            if (x1 > width) {
                x1 = width;
            }
            if (y1 > height) {
                y1 = height;
            }

            for (y = y0; y < y1; ++y) {
                for (x = x0; x < x1; ++x) {
                    sum += pixels[y * width + x];
                    count++;
                }
            }

            if (count > 0) {
                mesh_bg[my * nx + mx] = sum / (double)count;
            } else {
                mesh_bg[my * nx + mx] = 0.0;
            }
        }
    }

    {
        int fr = back_filtersize / 2;
        for (my = 0; my < ny; ++my) {
            for (mx = 0; mx < nx; ++mx) {
                int yy;
                int xx;
                double sum = 0.0;
                int count = 0;

                for (yy = my - fr; yy <= my + fr; ++yy) {
                    for (xx = mx - fr; xx <= mx + fr; ++xx) {
                        int cy = clamp_int(yy, 0, ny - 1);
                        int cx = clamp_int(xx, 0, nx - 1);
                        sum += mesh_bg[cy * nx + cx];
                        count++;
                    }
                }

                mesh_bg_filtered[my * nx + mx] = (count > 0) ? (sum / (double)count) : mesh_bg[my * nx + mx];
            }
        }
    }

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            double gx = (((double)x) + 0.5) / (double)back_size - 0.5;
            double gy = (((double)y) + 0.5) / (double)back_size - 0.5;
            int ix0 = (int)floor(gx);
            int iy0 = (int)floor(gy);
            int ix1;
            int iy1;
            double fx;
            double fy;
            double b00;
            double b10;
            double b01;
            double b11;
            double bg;

            ix0 = clamp_int(ix0, 0, nx - 1);
            iy0 = clamp_int(iy0, 0, ny - 1);
            ix1 = clamp_int(ix0 + 1, 0, nx - 1);
            iy1 = clamp_int(iy0 + 1, 0, ny - 1);

            fx = gx - (double)ix0;
            fy = gy - (double)iy0;
            if (fx < 0.0) {
                fx = 0.0;
            }
            if (fx > 1.0) {
                fx = 1.0;
            }
            if (fy < 0.0) {
                fy = 0.0;
            }
            if (fy > 1.0) {
                fy = 1.0;
            }

            b00 = mesh_bg_filtered[iy0 * nx + ix0];
            b10 = mesh_bg_filtered[iy0 * nx + ix1];
            b01 = mesh_bg_filtered[iy1 * nx + ix0];
            b11 = mesh_bg_filtered[iy1 * nx + ix1];

            bg = (1.0 - fx) * (1.0 - fy) * b00
                + fx * (1.0 - fy) * b10
                + (1.0 - fx) * fy * b01
                + fx * fy * b11;

            pixels[y * width + x] -= bg;
        }
    }

    return 0;
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

    if (local_cfg.back_size < SBSS_MIN_BACK_SIZE) {
        sbss_set_error(error_message, error_message_size, "BACK_SIZE must be >= %d", SBSS_MIN_BACK_SIZE);
        return -1;
    }

    if (local_cfg.back_filtersize < 1) {
        sbss_set_error(error_message, error_message_size, "BACK_FILTERSIZE must be >= 1");
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

    if (apply_background_subtraction(
            pixel_workspace,
            width,
            height,
            local_cfg.back_size,
            local_cfg.back_filtersize,
            error_message,
            error_message_size
        ) != 0) {
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
            detections[out_count].xmin = (int)((x - radius) < 0 ? 0 : (x - radius));
            detections[out_count].xmax = (int)((x + radius) >= width ? (width - 1) : (x + radius));
            detections[out_count].ymin = (int)((y - radius) < 0 ? 0 : (y - radius));
            detections[out_count].ymax = (int)((y + radius) >= height ? (height - 1) : (y + radius));
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
