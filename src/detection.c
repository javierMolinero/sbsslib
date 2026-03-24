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
static int g_component_labels[SBSS_MAX_IMAGE_PIXELS];
static int g_subcomponent_pixels[SBSS_MAX_IMAGE_PIXELS];
static int g_subcomponent_labels[SBSS_MAX_IMAGE_PIXELS];
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

static int compare_component_pixel_detect_desc(const void* a, const void* b) {
    int ia = *(const int*)a;
    int ib = *(const int*)b;
    double va = g_detect_pixels[ia];
    double vb = g_detect_pixels[ib];

    if (va < vb) {
        return 1;
    }
    if (va > vb) {
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

/* -----------------------------------------------------------------------
 * Elliptical aperture photometry helpers.
 * Mirrors SExtractor FLUX_AUTO: moments → ellipse → elliptical Kron radius
 * → elliptical aperture integration.
 * ----------------------------------------------------------------------- */

static void compute_second_order_moments(
    const int* pixel_indices,
    int n,
    long width,
    const double* residual,
    double center_x,
    double center_y,
    double* mxx,
    double* myy,
    double* mxy
) {
    int i;
    double sum_mxx = 0.0;
    double sum_myy = 0.0;
    double sum_mxy = 0.0;
    double sum_w   = 0.0;

    for (i = 0; i < n; ++i) {
        int idx = pixel_indices[i];
        int x   = idx % (int)width;
        int y   = idx / (int)width;
        double dx = (double)x - center_x;
        double dy = (double)y - center_y;
        double w  = residual[idx];
        if (w < 0.0) { w = 0.0; }
        sum_mxx += w * dx * dx;
        sum_myy += w * dy * dy;
        sum_mxy += w * dx * dy;
        sum_w   += w;
    }

    if (sum_w > 1e-12) {
        *mxx = sum_mxx / sum_w;
        *myy = sum_myy / sum_w;
        *mxy = sum_mxy / sum_w;
    } else {
        *mxx = 1.0;
        *myy = 1.0;
        *mxy = 0.0;
    }
}

/* Decompose moments into semi-axes A >= B and inverse-covariance coefficients
 * CXX, CYY, CXY  such that  CXX*dx^2 + CYY*dy^2 + CXY*dx*dy <= r^2  defines
 * an ellipse of "elliptical radius" r centred at the source centroid. */
static void moments_to_ellipse(
    double mxx,
    double myy,
    double mxy,
    double* a_out,
    double* b_out,
    double* cxx_out,
    double* cyy_out,
    double* cxy_out
) {
    double tmp = sqrt(((mxx - myy) * (mxx - myy)) / 4.0 + mxy * mxy);
    double a2  = (mxx + myy) / 2.0 + tmp;
    double b2  = (mxx + myy) / 2.0 - tmp;
    double a, b, theta, cos_t, sin_t;

    if (a2 < 0.0625) { a2 = 0.0625; }  /* floor at 0.25 px semi-axis */
    if (b2 < 0.0625) { b2 = 0.0625; }
    if (b2 > a2)     { b2 = a2; }

    a     = sqrt(a2);
    b     = sqrt(b2);
    theta = 0.5 * atan2(2.0 * mxy, mxx - myy);
    cos_t = cos(theta);
    sin_t = sin(theta);

    *cxx_out = (cos_t * cos_t) / a2 + (sin_t * sin_t) / b2;
    *cyy_out = (sin_t * sin_t) / a2 + (cos_t * cos_t) / b2;
    *cxy_out = 2.0 * cos_t * sin_t * (1.0 / a2 - 1.0 / b2);
    *a_out   = a;
    *b_out   = b;
}

/* Elliptical Kron radius: R_k = Sum(w * r_ellip) / Sum(w)          */
static double estimate_elliptical_kron_radius(
    const int* pixel_indices,
    int n,
    long width,
    const double* residual,
    double center_x,
    double center_y,
    double cxx,
    double cyy,
    double cxy
) {
    int i;
    double sum_rw = 0.0;
    double sum_w  = 0.0;
    double kron_radius;

    if (n < 2) { return 1.5; }

    for (i = 0; i < n; ++i) {
        int idx   = pixel_indices[i];
        int x     = idx % (int)width;
        int y     = idx / (int)width;
        double dx = (double)x - center_x;
        double dy = (double)y - center_y;
        double q  = cxx * dx * dx + cyy * dy * dy + cxy * dx * dy;
        double r  = (q > 0.0) ? sqrt(q) : 0.0;
        double w  = residual[idx];
        if (w < 0.0) { w = 0.0; }
        sum_rw += w * r;
        sum_w  += w;
    }

    kron_radius = (sum_w > 1e-12) ? (sum_rw / sum_w) : 1.5;
    if (kron_radius < 1.0) { kron_radius = 1.0; }
    if (kron_radius > 8.0) { kron_radius = 8.0; }
    return kron_radius;
}

/* Integrate flux within ellipse  CXX*dx^2 + CYY*dy^2 + CXY*dx*dy <= ap^2.
 * Bounding box is aperture_radius * a_semi + 2 pixels each side.          */
static double calculate_elliptical_aperture_flux(
    const double* image,
    long width,
    long height,
    double center_x,
    double center_y,
    double aperture_radius,
    double cxx,
    double cyy,
    double cxy,
    double a_semi
) {
    double r2_limit = aperture_radius * aperture_radius;
    int    bbox     = (int)ceil(aperture_radius * a_semi) + 2;
    int    dx, dy;
    double flux = 0.0;

    if (bbox > 60) { bbox = 60; }

    for (dy = -bbox; dy <= bbox; ++dy) {
        for (dx = -bbox; dx <= bbox; ++dx) {
            long   px  = (long)((int)center_x + dx);
            long   py  = (long)((int)center_y + dy);
            double fdx = (double)dx;
            double fdy = (double)dy;
            double q;

            if (px < 0 || px >= width || py < 0 || py >= height) { continue; }

            q = cxx * fdx * fdx + cyy * fdy * fdy + cxy * fdx * fdy;
            if (q > r2_limit) { continue; }

            flux += image[py * width + px];
        }
    }
    return flux;
}

static void fill_detection(
    sbss_detection* out,
    const int* pixel_indices,
    int n,
    long width,
    long height,
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
    double morph_flux;
    double aperture_flux;
    double peak = -1e99;
    double center_x;
    double center_y;
    double mxx, myy, mxy;
    double a_ax, b_ax;
    double cxx, cyy, cxy;
    double kron_radius;
    double ap;

    /* Step 1: basic stats — centroid, bbox, peak, morphological flux */
    for (i = 0; i < n; ++i) {
        int idx = pixel_indices[i];
        int x   = idx % (int)width;
        int y   = idx / (int)width;
        double rv = residual[idx];
        double dv = detect[idx];
        double w  = (rv > 0.0) ? rv : 0.0;

        if (x < xmin) { xmin = x; }
        if (x > xmax) { xmax = x; }
        if (y < ymin) { ymin = y; }
        if (y > ymax) { ymax = y; }

        flux  += w;
        wx    += w * (double)x;
        wy    += w * (double)y;
        wsum  += w;
        if (dv > peak) { peak = dv; }
    }

    /* Step 2: centroid */
    center_x = (wsum > 1e-12) ? (wx / wsum) : (double)xmin;
    center_y = (wsum > 1e-12) ? (wy / wsum) : (double)ymin;

    /* Step 3: second-order moments → ellipse axes and inverse-covariance */
    compute_second_order_moments(
        pixel_indices, n, width, residual, center_x, center_y,
        &mxx, &myy, &mxy
    );
    moments_to_ellipse(mxx, myy, mxy, &a_ax, &b_ax, &cxx, &cyy, &cxy);

    /* Step 4: elliptical Kron radius → aperture in elliptical-radius units */
    kron_radius = estimate_elliptical_kron_radius(
        pixel_indices, n, width, residual, center_x, center_y, cxx, cyy, cxy
    );
    ap = 2.5 * kron_radius;
    if (ap < 3.5 / a_ax) { ap = 3.5 / a_ax; }  /* enforce min ~3.5 px radius */

    /* Step 5: elliptical aperture flux */
    aperture_flux = calculate_elliptical_aperture_flux(
        residual, width, height, center_x, center_y, ap, cxx, cyy, cxy, a_ax
    );

    morph_flux = flux;

    /* Use aperture flux; cap to 1.55x morphological to suppress blend runaway */
    if (aperture_flux > 0.0) {
        double cap = (morph_flux > 0.0) ? (1.55 * morph_flux) : aperture_flux;
        flux = (aperture_flux > cap) ? cap : aperture_flux;
    }

    out->x    = center_x;
    out->y    = center_y;
    out->xmin = xmin;
    out->xmax = xmax;
    out->ymin = ymin;
    out->ymax = ymax;
    out->peak = peak;
    out->flux = flux;
    out->area = n;
}

static size_t emit_component_with_deblend(
    sbss_detection* detections,
    size_t detections_capacity,
    size_t out_count,
    const sbss_config* cfg,
    int component_size,
    long width,
    long height,
    const double* residual,
    const double* detect
) {
    int i;
    int minarea;
    int nthresh;
    int node_count = 0;
    int terminal_count = 0;
    int keep_count = 0;
    double mincont;
    double component_flux = 0.0;
    double component_min_detect = 1e99;
    double component_max_detect = -1e99;
    static int level_region_area[SBSS_MAX_DETECTIONS];
    static double level_region_flux[SBSS_MAX_DETECTIONS];
    static double level_region_peak[SBSS_MAX_DETECTIONS];
    static int level_region_node[SBSS_MAX_DETECTIONS];
    static int node_parent[SBSS_MAX_DETECTIONS];
    static int node_first_child[SBSS_MAX_DETECTIONS];
    static int node_next_sibling[SBSS_MAX_DETECTIONS];
    static int node_level[SBSS_MAX_DETECTIONS];
    static double node_threshold[SBSS_MAX_DETECTIONS];
    static int node_area[SBSS_MAX_DETECTIONS];
    static double node_flux[SBSS_MAX_DETECTIONS];
    static double node_peak[SBSS_MAX_DETECTIONS];
    static int node_seed_idx[SBSS_MAX_DETECTIONS];
    static int node_selected[SBSS_MAX_DETECTIONS];
    static int node_leaf[SBSS_MAX_DETECTIONS];
    static double node_saddle_flux[SBSS_MAX_DETECTIONS];
    static double node_saddle_total[SBSS_MAX_DETECTIONS];
    static int child_nodes[SBSS_MAX_DETECTIONS];
    static double child_seed_x[SBSS_MAX_DETECTIONS];
    static double child_seed_y[SBSS_MAX_DETECTIONS];
    static int region_pixels[SBSS_MAX_DETECTIONS];
    static int assign_queue[SBSS_MAX_DETECTIONS];
    static int terminal_nodes[SBSS_MAX_DETECTIONS];
    static double seed_peak[SBSS_MAX_DETECTIONS];
    static double seed_cx[SBSS_MAX_DETECTIONS];
    static double seed_cy[SBSS_MAX_DETECTIONS];
    static int final_area[SBSS_MAX_DETECTIONS];
    static double final_flux[SBSS_MAX_DETECTIONS];

    if (component_size < cfg->detect_minarea) {
        return out_count;
    }

    minarea = (cfg->detect_minarea < 1) ? 1 : cfg->detect_minarea;
    mincont = cfg->deblend_mincont;
    if (mincont < 0.0) {
        mincont = 0.0;
    }
    if (mincont > 1.0) {
        mincont = 1.0;
    }

    nthresh = cfg->deblend_nthresh;
    if (nthresh < 2) {
        nthresh = 2;
    }
    if (nthresh > 64) {
        nthresh = 64;
    }

    qsort(g_component_pixels, (size_t)component_size, sizeof(int), compare_component_pixel_detect_desc);

    for (i = 0; i < component_size; ++i) {
        int idx = g_component_pixels[i];
        double dv = detect[idx];
        g_component_labels[idx] = 0;
        g_subcomponent_labels[idx] = 0;

        if (dv < component_min_detect) {
            component_min_detect = dv;
        }
        if (dv > component_max_detect) {
            component_max_detect = dv;
        }
        if (residual[idx] > 0.0) {
            component_flux += residual[idx];
        }
    }

    /* Build explicit parent-child tree from threshold ladder (low -> high). */
    if (component_max_detect > component_min_detect + 1e-9) {
        int level_idx;
        for (level_idx = 1; level_idx <= nthresh; ++level_idx) {
            int region_count = 0;
            double frac = ((double)level_idx) / (double)(nthresh + 1);
            double level = component_min_detect + frac * (component_max_detect - component_min_detect);

            for (i = 0; i < component_size; ++i) {
                int idx = g_component_pixels[i];
                g_component_labels[idx] = -1;
            }

            for (i = 0; i < component_size; ++i) {
                int idx = g_component_pixels[i];
                int head;
                int tail;
                int parent_candidate = 0;

                if (detect[idx] < level || g_component_labels[idx] != -1) {
                    continue;
                }
                if (region_count + 1 >= (int)SBSS_MAX_DETECTIONS) {
                    break;
                }

                region_count++;
                level_region_area[region_count] = 0;
                level_region_flux[region_count] = 0.0;
                level_region_peak[region_count] = -1e99;
                level_region_node[region_count] = 0;

                head = 0;
                tail = 0;
                g_subcomponent_pixels[tail++] = idx;
                g_component_labels[idx] = region_count;

                while (head < tail) {
                    int cidx = g_subcomponent_pixels[head++];
                    int cx = cidx % (int)width;
                    int cy = cidx / (int)width;
                    int yy;
                    int xx;

                    level_region_area[region_count] += 1;
                    if (residual[cidx] > 0.0) {
                        level_region_flux[region_count] += residual[cidx];
                    }
                    if (detect[cidx] > level_region_peak[region_count]) {
                        level_region_peak[region_count] = detect[cidx];
                    }
                    if (g_subcomponent_labels[cidx] > 0) {
                        parent_candidate = g_subcomponent_labels[cidx];
                    }

                    for (yy = cy - 1; yy <= cy + 1; ++yy) {
                        for (xx = cx - 1; xx <= cx + 1; ++xx) {
                            int nidx;
                            if (xx < 0 || yy < 0 || xx >= (int)width || yy >= (int)height) {
                                continue;
                            }
                            if (xx == cx && yy == cy) {
                                continue;
                            }

                            nidx = yy * (int)width + xx;
                            if (g_component_labels[nidx] != -1 || detect[nidx] < level) {
                                continue;
                            }

                            g_component_labels[nidx] = region_count;
                            if (tail < component_size) {
                                g_subcomponent_pixels[tail++] = nidx;
                            }
                        }
                    }
                }

                if (node_count + 1 >= (int)SBSS_MAX_DETECTIONS) {
                    continue;
                }

                node_count++;
                level_region_node[region_count] = node_count;
                node_parent[node_count] = parent_candidate;
                node_first_child[node_count] = 0;
                node_next_sibling[node_count] = 0;
                node_level[node_count] = level_idx;
                node_threshold[node_count] = level;
                node_area[node_count] = level_region_area[region_count];
                node_flux[node_count] = level_region_flux[region_count];
                node_peak[node_count] = level_region_peak[region_count];
                node_selected[node_count] = 0;
                node_leaf[node_count] = 1;
                node_seed_idx[node_count] = idx;

                if (parent_candidate > 0 && parent_candidate <= node_count) {
                    int p = parent_candidate;
                    node_leaf[p] = 0;
                    node_next_sibling[node_count] = node_first_child[p];
                    node_first_child[p] = node_count;
                }
            }

            for (i = 0; i < component_size; ++i) {
                int idx = g_component_pixels[i];
                int rid = g_component_labels[idx];
                if (rid > 0 && rid <= region_count) {
                    g_subcomponent_labels[idx] = level_region_node[rid];
                } else {
                    g_subcomponent_labels[idx] = 0;
                }
            }
        }
    }

    if (node_count < 2) {
        if (out_count < detections_capacity) {
            fill_detection(
                &detections[out_count],
                g_component_pixels,
                component_size,
                width,
                height,
                residual,
                detect
            );
            out_count++;
        }
        for (i = 0; i < component_size; ++i) {
            int idx = g_component_pixels[i];
            g_component_labels[idx] = 0;
            g_subcomponent_labels[idx] = 0;
        }
        return out_count;
    }

    /* Mark all current component pixels for fast membership checks. */
    for (i = 1; i <= node_count; ++i) {
        node_saddle_flux[i] = 0.0;
        node_saddle_total[i] = 0.0;
    }
    for (i = 0; i < component_size; ++i) {
        int idx = g_component_pixels[i];
        g_component_labels[idx] = 1;
        g_subcomponent_labels[idx] = 0;
    }

    /*
     * True saddle-level accumulation per sibling set:
     * for each node, distribute parent-region flux (at parent threshold) to its
     * direct children by seeded growth, and cache per-child saddle flux.
     */
    for (i = 1; i <= node_count; ++i) {
        int n = i;
        int child = node_first_child[n];
        int nchildren = 0;

        while (child > 0 && nchildren < (int)SBSS_MAX_DETECTIONS - 1) {
            if (node_area[child] >= minarea) {
                child_nodes[nchildren] = child;
                child_seed_x[nchildren] = (double)(node_seed_idx[child] % (int)width);
                child_seed_y[nchildren] = (double)(node_seed_idx[child] / (int)width);
                nchildren++;
            }
            child = node_next_sibling[child];
        }

        if (nchildren < 2) {
            continue;
        }

        {
            int h = 0;
            int t = 0;
            int region_count = 0;
            int seed_head = 0;
            int seed_tail = 0;
            int parent_seed = node_seed_idx[n];
            double saddle = node_threshold[n];
            int s;

            if (parent_seed < 0 || parent_seed >= (int)(width * height)) {
                continue;
            }

            /* Build parent region at exact merge threshold. */
            g_subcomponent_labels[parent_seed] = -1;
            assign_queue[t++] = parent_seed;
            while (h < t && region_count < component_size) {
                int cidx = assign_queue[h++];
                int cx = cidx % (int)width;
                int cy = cidx / (int)width;
                int yy;
                int xx;

                region_pixels[region_count++] = cidx;

                for (yy = cy - 1; yy <= cy + 1; ++yy) {
                    for (xx = cx - 1; xx <= cx + 1; ++xx) {
                        int nidx;
                        if (xx < 0 || yy < 0 || xx >= (int)width || yy >= (int)height) {
                            continue;
                        }
                        if (xx == cx && yy == cy) {
                            continue;
                        }
                        nidx = yy * (int)width + xx;
                        if (g_component_labels[nidx] != 1) {
                            continue;
                        }
                        if (g_subcomponent_labels[nidx] != 0) {
                            continue;
                        }
                        if (detect[nidx] < saddle) {
                            continue;
                        }
                        g_subcomponent_labels[nidx] = -1;
                        assign_queue[t++] = nidx;
                    }
                }
            }

            for (s = 0; s < nchildren; ++s) {
                int seed_idx = node_seed_idx[child_nodes[s]];
                if (seed_idx >= 0 && seed_idx < (int)(width * height) && g_subcomponent_labels[seed_idx] == -1) {
                    g_subcomponent_labels[seed_idx] = s + 1;
                    assign_queue[seed_tail++] = seed_idx;
                }
            }

            /* Multi-source growth inside parent region mask. */
            while (seed_head < seed_tail) {
                int cidx = assign_queue[seed_head++];
                int cx = cidx % (int)width;
                int cy = cidx / (int)width;
                int lab = g_subcomponent_labels[cidx];
                int yy;
                int xx;

                for (yy = cy - 1; yy <= cy + 1; ++yy) {
                    for (xx = cx - 1; xx <= cx + 1; ++xx) {
                        int nidx;
                        if (xx < 0 || yy < 0 || xx >= (int)width || yy >= (int)height) {
                            continue;
                        }
                        if (xx == cx && yy == cy) {
                            continue;
                        }
                        nidx = yy * (int)width + xx;
                        if (g_subcomponent_labels[nidx] != -1) {
                            continue;
                        }
                        g_subcomponent_labels[nidx] = lab;
                        assign_queue[seed_tail++] = nidx;
                    }
                }
            }

            /* Accumulate flux per child branch at this saddle threshold. */
            for (s = 0; s < nchildren; ++s) {
                node_saddle_flux[child_nodes[s]] = 0.0;
            }
            node_saddle_total[n] = 0.0;

            for (s = 0; s < region_count; ++s) {
                int pidx = region_pixels[s];
                int lab = g_subcomponent_labels[pidx];
                int use_lab = lab;
                double f = (residual[pidx] > 0.0) ? residual[pidx] : 0.0;

                if (use_lab <= 0) {
                    int px = pidx % (int)width;
                    int py = pidx / (int)width;
                    int best = 1;
                    double best_d2 = 1e99;
                    int c;
                    for (c = 0; c < nchildren; ++c) {
                        double dx = (double)px - child_seed_x[c];
                        double dy = (double)py - child_seed_y[c];
                        double d2 = dx * dx + dy * dy;
                        if (d2 < best_d2) {
                            best_d2 = d2;
                            best = c + 1;
                        }
                    }
                    use_lab = best;
                    g_subcomponent_labels[pidx] = use_lab;
                }

                if (use_lab > 0 && use_lab <= nchildren) {
                    int child_node = child_nodes[use_lab - 1];
                    node_saddle_flux[child_node] += f;
                    node_saddle_total[n] += f;
                }
            }

            /* Clear temporary region marks before next parent. */
            for (s = 0; s < region_count; ++s) {
                int pidx = region_pixels[s];
                g_subcomponent_labels[pidx] = 0;
            }
        }
    }

    /* Tree split decision using cached saddle-level child fluxes. */
    {
        int n;
        for (n = 1; n <= node_count; ++n) {
            node_selected[n] = 0;
        }

        for (n = node_count; n >= 1; --n) {
            int child = node_first_child[n];
            int good_children = 0;
            double saddle_total = node_saddle_total[n];

            while (child > 0) {
                if (node_area[child] >= minarea) {
                    double child_flux = node_saddle_flux[child];
                    if (saddle_total <= 1e-12 || child_flux >= (mincont * saddle_total)) {
                        good_children++;
                    }
                }
                child = node_next_sibling[child];
            }

            if (good_children >= 2) {
                child = node_first_child[n];
                while (child > 0) {
                    if (node_area[child] >= minarea) {
                        double child_flux = node_saddle_flux[child];
                        if (saddle_total <= 1e-12 || child_flux >= (mincont * saddle_total)) {
                            node_selected[child] = 1;
                        }
                    }
                    child = node_next_sibling[child];
                }
            } else {
                node_selected[n] = 1;
            }
        }
    }

    for (i = 1; i <= node_count; ++i) {
        if (node_selected[i] && node_area[i] >= minarea) {
            terminal_nodes[terminal_count++] = i;
            if (terminal_count >= (int)SBSS_MAX_DETECTIONS - 1) {
                break;
            }
        }
    }

    if (terminal_count <= 1) {
        if (out_count < detections_capacity) {
            fill_detection(
                &detections[out_count],
                g_component_pixels,
                component_size,
                width,
                height,
                residual,
                detect
            );
            out_count++;
        }
        for (i = 0; i < component_size; ++i) {
            int idx = g_component_pixels[i];
            g_component_labels[idx] = 0;
            g_subcomponent_labels[idx] = 0;
        }
        return out_count;
    }

    for (i = 0; i < component_size; ++i) {
        int idx = g_component_pixels[i];
        g_component_labels[idx] = 0;
    }

    for (i = 1; i <= terminal_count; ++i) {
        int n = terminal_nodes[i - 1];
        int seed_idx = node_seed_idx[n];
        int sx = seed_idx % (int)width;
        int sy = seed_idx / (int)width;
        g_component_labels[seed_idx] = i;
        seed_peak[i] = node_peak[n];
        seed_cx[i] = (double)sx;
        seed_cy[i] = (double)sy;
        final_area[i] = 1;
    }

    for (i = 0; i < component_size; ++i) {
        int idx = g_component_pixels[i];
        int cx = idx % (int)width;
        int cy = idx / (int)width;
        int yy;
        int xx;
        int chosen = g_component_labels[idx];
        double best_peak = -1e99;

        if (chosen > 0) {
            continue;
        }

        for (yy = cy - 1; yy <= cy + 1; ++yy) {
            for (xx = cx - 1; xx <= cx + 1; ++xx) {
                int nidx;
                int nl;
                if (xx == cx && yy == cy) {
                    continue;
                }
                if (xx < 0 || yy < 0 || xx >= (int)width || yy >= (int)height) {
                    continue;
                }
                nidx = yy * (int)width + xx;
                nl = g_component_labels[nidx];
                if (nl > 0 && seed_peak[nl] > best_peak) {
                    best_peak = seed_peak[nl];
                    chosen = nl;
                }
            }
        }

        if (chosen > 0) {
            g_component_labels[idx] = chosen;
            final_area[chosen] += 1;
            seed_cx[chosen] += (double)cx;
            seed_cy[chosen] += (double)cy;
        }
    }

    for (i = 1; i <= terminal_count; ++i) {
        if (final_area[i] > 0) {
            seed_cx[i] /= (double)final_area[i];
            seed_cy[i] /= (double)final_area[i];
        }
    }

    for (i = 0; i < component_size; ++i) {
        int idx = g_component_pixels[i];
        if (g_component_labels[idx] <= 0) {
            int x = idx % (int)width;
            int y = idx / (int)width;
            int l;
            int chosen = 1;
            double best_d2 = 1e99;
            for (l = 1; l <= terminal_count; ++l) {
                double dx = (double)x - seed_cx[l];
                double dy = (double)y - seed_cy[l];
                double d2 = dx * dx + dy * dy;
                if (d2 < best_d2) {
                    best_d2 = d2;
                    chosen = l;
                }
            }
            g_component_labels[idx] = chosen;
        }
    }

    for (i = 1; i <= terminal_count; ++i) {
        final_area[i] = 0;
        final_flux[i] = 0.0;
    }
    for (i = 0; i < component_size; ++i) {
        int idx = g_component_pixels[i];
        int lid = g_component_labels[idx];
        if (lid > 0 && lid <= terminal_count) {
            final_area[lid] += 1;
            if (residual[idx] > 0.0) {
                final_flux[lid] += residual[idx];
            }
        }
    }

    keep_count = 0;
    for (i = 1; i <= terminal_count; ++i) {
        if (final_area[i] < minarea) {
            continue;
        }
        if (component_flux > 1e-12 && final_flux[i] < (mincont * component_flux)) {
            continue;
        }
        terminal_nodes[keep_count++] = i;
    }

    if (keep_count <= 1) {
        if (out_count < detections_capacity) {
            fill_detection(
                &detections[out_count],
                g_component_pixels,
                component_size,
                width,
                height,
                residual,
                detect
            );
            out_count++;
        }
    } else {
        int k;
        for (k = 0; k < keep_count && out_count < detections_capacity; ++k) {
            int nsub = 0;
            int label = terminal_nodes[k];

            for (i = 0; i < component_size; ++i) {
                int idx = g_component_pixels[i];
                if (g_component_labels[idx] == label) {
                    g_subcomponent_pixels[nsub++] = idx;
                }
            }

            if (nsub >= minarea) {
                fill_detection(
                    &detections[out_count],
                    g_subcomponent_pixels,
                    nsub,
                    width,
                    height,
                    residual,
                    detect
                );
                out_count++;
            }
        }
    }

    for (i = 0; i < component_size; ++i) {
        int idx = g_component_pixels[i];
        g_component_labels[idx] = 0;
        g_subcomponent_labels[idx] = 0;
    }

    return out_count;
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
                out_count = emit_component_with_deblend(
                    detections,
                    detections_capacity,
                    out_count,
                    &local_cfg,
                    tail,
                    width,
                    height,
                    g_pixels,
                    g_detect_pixels
                );
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
