#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define SBSS_MIN_BACK_SIZE 8
#define SBSS_MAX_MESH_X 1024
#define SBSS_MAX_MESH_Y 1024
#define SBSS_MAX_MESH_COUNT (SBSS_MAX_MESH_X * SBSS_MAX_MESH_Y)

static double g_pixels[SBSS_MAX_IMAGE_PIXELS];
static double g_detect_pixels[SBSS_MAX_IMAGE_PIXELS];
static unsigned char g_visited[SBSS_MAX_IMAGE_PIXELS];
static int g_component_pixels[SBSS_MAX_IMAGE_PIXELS];
static double g_mesh_bg[SBSS_MAX_MESH_COUNT];
static double g_mesh_rms[SBSS_MAX_MESH_COUNT];
static double g_mesh_bg_smooth[SBSS_MAX_MESH_COUNT];
static double g_mesh_rms_smooth[SBSS_MAX_MESH_COUNT];

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

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void robust_mean_rms(const double* values, int n, double* out_mean, double* out_rms) {
    int i;
    int iter;
    double mean = 0.0;
    double rms;

    for (i = 0; i < n; ++i) {
        mean += values[i];
    }
    mean /= (double)n;

    rms = 0.0;
    for (i = 0; i < n; ++i) {
        double d = values[i] - mean;
        rms += d * d;
    }
    rms = sqrt(rms / (double)n);

    for (iter = 0; iter < 3; ++iter) {
        double s = 0.0;
        double s2 = 0.0;
        int used = 0;
        double low = mean - 3.0 * rms;
        double high = mean + 3.0 * rms;

        for (i = 0; i < n; ++i) {
            if (values[i] >= low && values[i] <= high) {
                s += values[i];
                s2 += values[i] * values[i];
                used++;
            }
        }

        if (used < 8) {
            break;
        }

        mean = s / (double)used;
        rms = s2 / (double)used - mean * mean;
        if (rms < 1e-20) {
            rms = 1e-20;
        }
        rms = sqrt(rms);
    }

    *out_mean = mean;
    *out_rms = rms;
}

static void interpolate_mesh_value(
    const double* mesh,
    int nx,
    int ny,
    int back_size,
    long x,
    long y,
    double* out_value
) {
    double gx = (((double)x) + 0.5) / (double)back_size - 0.5;
    double gy = (((double)y) + 0.5) / (double)back_size - 0.5;
    int ix0 = (int)floor(gx);
    int iy0 = (int)floor(gy);
    int ix1;
    int iy1;
    double fx;
    double fy;

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

    *out_value = (1.0 - fx) * (1.0 - fy) * mesh[iy0 * nx + ix0]
        + fx * (1.0 - fy) * mesh[iy0 * nx + ix1]
        + (1.0 - fx) * fy * mesh[iy1 * nx + ix0]
        + fx * fy * mesh[iy1 * nx + ix1];
}

static int build_background_model(
    double* pixels,
    long width,
    long height,
    int back_size,
    int back_filtersize,
    int* out_nx,
    int* out_ny,
    char* error_message,
    size_t error_message_size
) {
    int nx;
    int ny;
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
    if (nx < 1 || ny < 1 || nx > SBSS_MAX_MESH_X || ny > SBSS_MAX_MESH_Y) {
        sbss_set_error(error_message, error_message_size, "invalid mesh dimensions (%d x %d)", nx, ny);
        return -1;
    }

    for (my = 0; my < ny; ++my) {
        for (mx = 0; mx < nx; ++mx) {
            long x0 = (long)mx * (long)back_size;
            long y0 = (long)my * (long)back_size;
            long x1 = x0 + (long)back_size;
            long y1 = y0 + (long)back_size;
            double mean;
            double rms;
            int c = 0;
            static double cell_values[128 * 128];

            if (x1 > width) {
                x1 = width;
            }
            if (y1 > height) {
                y1 = height;
            }

            for (y = y0; y < y1; ++y) {
                for (x = x0; x < x1; ++x) {
                    if (c < (int)(sizeof(cell_values) / sizeof(cell_values[0]))) {
                        cell_values[c++] = pixels[y * width + x];
                    }
                }
            }

            if (c < 8) {
                mean = 0.0;
                rms = 1.0;
            } else {
                robust_mean_rms(cell_values, c, &mean, &rms);
            }

            g_mesh_bg[my * nx + mx] = mean;
            g_mesh_rms[my * nx + mx] = (rms > 1e-6) ? rms : 1e-6;
        }
    }

    {
        int fr = back_filtersize / 2;
        for (my = 0; my < ny; ++my) {
            for (mx = 0; mx < nx; ++mx) {
                int yy;
                int xx;
                double s_bg = 0.0;
                double s_rms = 0.0;
                int cnt = 0;

                for (yy = my - fr; yy <= my + fr; ++yy) {
                    for (xx = mx - fr; xx <= mx + fr; ++xx) {
                        int cy = clamp_int(yy, 0, ny - 1);
                        int cx = clamp_int(xx, 0, nx - 1);
                        s_bg += g_mesh_bg[cy * nx + cx];
                        s_rms += g_mesh_rms[cy * nx + cx];
                        cnt++;
                    }
                }

                g_mesh_bg_smooth[my * nx + mx] = s_bg / (double)cnt;
                g_mesh_rms_smooth[my * nx + mx] = s_rms / (double)cnt;
            }
        }
    }

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            double bg;
            interpolate_mesh_value(g_mesh_bg_smooth, nx, ny, back_size, x, y, &bg);
            pixels[y * width + x] -= bg;
        }
    }

    *out_nx = nx;
    *out_ny = ny;
    return 0;
}

static void apply_convolution(const sbss_config* cfg, const double* src, double* dst, long width, long height) {
    long x;
    long y;

    if (cfg->filter_enabled == 0 || cfg->filter_kernel_size <= 1) {
        (void)memcpy(dst, src, (size_t)(width * height) * sizeof(double));
        return;
    }

    {
        int ks = cfg->filter_kernel_size;
        int kr = ks / 2;
        for (y = 0; y < height; ++y) {
            for (x = 0; x < width; ++x) {
                int ky;
                int kx;
                double sum = 0.0;

                for (ky = -kr; ky <= kr; ++ky) {
                    for (kx = -kr; kx <= kr; ++kx) {
                        long sx = (long)clamp_int((int)x + kx, 0, (int)width - 1);
                        long sy = (long)clamp_int((int)y + ky, 0, (int)height - 1);
                        double w = cfg->filter_kernel[(ky + kr) * ks + (kx + kr)];
                        sum += src[sy * width + sx] * w;
                    }
                }

                dst[y * width + x] = sum;
            }
        }
    }
}

static int threshold_exceeded(
    int idx,
    long width,
    int nx,
    int ny,
    int back_size,
    double detect_thresh_sigma,
    const double* detect_pixels
) {
    long x = idx % (int)width;
    long y = idx / (int)width;
    double local_rms;

    interpolate_mesh_value(g_mesh_rms_smooth, nx, ny, back_size, x, y, &local_rms);
    if (local_rms < 1e-6) {
        local_rms = 1e-6;
    }

    return detect_pixels[idx] > (detect_thresh_sigma * local_rms);
}

static void fill_detection(
    sbss_detection* out,
    const int* pixel_indices,
    int n,
    long width,
    const double* residual,
    const double* detect
) {
    int i;
    int xmin = (int)width;
    int xmax = 0;
    int ymin = 2147483647;
    int ymax = 0;
    double wx = 0.0;
    double wy = 0.0;
    double wsum = 0.0;
    double flux = 0.0;
    double peak = -1e99;

    for (i = 0; i < n; ++i) {
        int idx = pixel_indices[i];
        int x = idx % (int)width;
        int y = idx / (int)width;
        double rv = residual[idx];
        double dv = detect[idx];
        double w = (rv > 0.0) ? rv : 0.0;

        if (x < xmin) {
            xmin = x;
        }
        if (x > xmax) {
            xmax = x;
        }
        if (y < ymin) {
            ymin = y;
        }
        if (y > ymax) {
            ymax = y;
        }

        flux += w;
        wx += w * (double)x;
        wy += w * (double)y;
        wsum += w;
        if (dv > peak) {
            peak = dv;
        }
    }

    out->x = (wsum > 1e-12) ? (wx / wsum) : (double)xmin;
    out->y = (wsum > 1e-12) ? (wy / wsum) : (double)ymin;
    out->xmin = xmin;
    out->xmax = xmax;
    out->ymin = ymin;
    out->ymax = ymax;
    out->peak = peak;
    out->flux = flux;
    out->area = n;
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
    sbss_config local_cfg;
    long width = 0;
    long height = 0;
    long n_pix;
    long idx;
    int nx = 0;
    int ny = 0;
    size_t out_count = 0;

    if (detections == NULL || detection_count == NULL || fits_path == NULL) {
        sbss_set_error(error_message, error_message_size, "invalid detection input");
        return -1;
    }

    if (cfg != NULL) {
        local_cfg = *cfg;
    } else {
        sbss_config_set_defaults(&local_cfg);
    }

    if (local_cfg.detect_minarea < 1 || local_cfg.detect_thresh_sigma <= 0.0) {
        sbss_set_error(error_message, error_message_size, "invalid detect parameters");
        return -1;
    }

    if (local_cfg.max_sources < 1 || (size_t)local_cfg.max_sources > detections_capacity) {
        sbss_set_error(error_message, error_message_size, "invalid MAX_SOURCES vs capacity");
        return -1;
    }

    if (sbss_read_fits_image(
            fits_path,
            g_pixels,
            SBSS_MAX_IMAGE_PIXELS,
            &width,
            &height,
            error_message,
            error_message_size
        ) != 0) {
        return -1;
    }

    n_pix = width * height;
    if (n_pix <= 0 || (size_t)n_pix > SBSS_MAX_IMAGE_PIXELS) {
        sbss_set_error(error_message, error_message_size, "invalid image size");
        return -1;
    }

    if (build_background_model(
            g_pixels,
            width,
            height,
            local_cfg.back_size,
            local_cfg.back_filtersize,
            &nx,
            &ny,
            error_message,
            error_message_size
        ) != 0) {
        return -1;
    }

    apply_convolution(&local_cfg, g_pixels, g_detect_pixels, width, height);

    (void)memset(g_visited, 0, (size_t)n_pix);
    (void)memset(detections, 0, detections_capacity * sizeof(sbss_detection));

    for (idx = 0; idx < n_pix; ++idx) {
        if (out_count >= (size_t)local_cfg.max_sources) {
            break;
        }

        if (g_visited[idx] != 0) {
            continue;
        }

        if (threshold_exceeded((int)idx, width, nx, ny, local_cfg.back_size, local_cfg.detect_thresh_sigma, g_detect_pixels) == 0) {
            continue;
        }

        {
            int head = 0;
            int tail = 0;
            int i;

            g_component_pixels[tail++] = (int)idx;
            g_visited[idx] = 1;

            while (head < tail) {
                int cidx = g_component_pixels[head++];
                int cx = cidx % (int)width;
                int cy = cidx / (int)width;
                int yy;
                int xx;

                for (yy = cy - 1; yy <= cy + 1; ++yy) {
                    for (xx = cx - 1; xx <= cx + 1; ++xx) {
                        int nidx;
                        if (xx < 0 || yy < 0 || xx >= width || yy >= height) {
                            continue;
                        }
                        nidx = yy * (int)width + xx;
                        if (g_visited[nidx] != 0) {
                            continue;
                        }

                        if (threshold_exceeded(nidx, width, nx, ny, local_cfg.back_size, local_cfg.detect_thresh_sigma, g_detect_pixels) != 0) {
                            g_visited[nidx] = 1;
                            if (tail < (int)SBSS_MAX_IMAGE_PIXELS) {
                                g_component_pixels[tail++] = nidx;
                            }
                        }
                    }
                }
            }

            if (tail >= local_cfg.detect_minarea && out_count < detections_capacity) {
                fill_detection(
                    &detections[out_count],
                    g_component_pixels,
                    tail,
                    width,
                    g_pixels,
                    g_detect_pixels
                );
                out_count++;
            }

            for (i = 0; i < tail; ++i) {
                g_visited[g_component_pixels[i]] = 1;
            }
        }
    }

    qsort(detections, out_count, sizeof(sbss_detection), compare_detection_peak_desc);
    *detection_count = out_count;
    return 0;
}
