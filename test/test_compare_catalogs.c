#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct catalog_row {
    double x;
    double y;
    int xmin;
    int xmax;
    int ymin;
    int ymax;
    double flux;
    int used;
} catalog_row;

#define MAX_ROWS 200000

static int load_catalog(const char* path, catalog_row* rows, size_t* count, char* error, size_t error_size) {
    FILE* f = NULL;
    char line[1024];
    size_t n = 0;

    if (path == NULL || rows == NULL || count == NULL) {
        (void)snprintf(error, error_size, "invalid load_catalog input");
        return -1;
    }

    f = fopen(path, "r");
    if (f == NULL) {
        (void)snprintf(error, error_size, "cannot open catalog: %s", path);
        return -1;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        catalog_row r;
        int parsed = 0;

        if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') {
            continue;
        }

        parsed = sscanf(
            line,
            "%lf %lf %d %d %d %d %lf",
            &r.x,
            &r.y,
            &r.xmin,
            &r.xmax,
            &r.ymin,
            &r.ymax,
            &r.flux
        );

        if (parsed < 7) {
            continue;
        }

        if (n >= MAX_ROWS) {
            (void)snprintf(error, error_size, "catalog exceeds MAX_ROWS (%d)", MAX_ROWS);
            fclose(f);
            return -1;
        }

        r.used = 0;
        rows[n++] = r;
    }

    fclose(f);
    *count = n;
    return 0;
}

int main(int argc, char** argv) {
    static catalog_row sbss_rows[MAX_ROWS];
    static catalog_row sx_rows[MAX_ROWS];
    char error[256] = {0};
    size_t sbss_n = 0;
    size_t sx_n = 0;
    size_t i;
    size_t match_count = 0;
    double sum_abs_dx = 0.0;
    double sum_abs_dy = 0.0;
    double sum_rel_flux = 0.0;
    const double max_match_distance = 3.0;

    if (argc != 3) {
        fprintf(stderr, "usage: test_compare_catalogs <sbss_catalog> <sx_catalog>\n");
        return 1;
    }

    if (load_catalog(argv[1], sbss_rows, &sbss_n, error, sizeof(error)) != 0) {
        fprintf(stderr, "load error (sbss): %s\n", error);
        return 1;
    }

    if (load_catalog(argv[2], sx_rows, &sx_n, error, sizeof(error)) != 0) {
        fprintf(stderr, "load error (sx): %s\n", error);
        return 1;
    }

    if (sbss_n == 0 || sx_n == 0) {
        fprintf(stderr, "empty catalog(s): sbss=%zu sx=%zu\n", sbss_n, sx_n);
        return 1;
    }

    for (i = 0; i < sbss_n; ++i) {
        size_t j;
        size_t best_j = (size_t)(-1);
        double best_d2 = 1e99;

        for (j = 0; j < sx_n; ++j) {
            double dx;
            double dy;
            double d2;

            if (sx_rows[j].used != 0) {
                continue;
            }

            dx = sbss_rows[i].x - sx_rows[j].x;
            dy = sbss_rows[i].y - sx_rows[j].y;
            d2 = (dx * dx) + (dy * dy);

            if (d2 < best_d2) {
                best_d2 = d2;
                best_j = j;
            }
        }

        if (best_j != (size_t)(-1) && best_d2 <= (max_match_distance * max_match_distance)) {
            double abs_dx = fabs(sbss_rows[i].x - sx_rows[best_j].x);
            double abs_dy = fabs(sbss_rows[i].y - sx_rows[best_j].y);
            double rel_flux;

            sx_rows[best_j].used = 1;
            match_count++;

            if (fabs(sx_rows[best_j].flux) > 1e-12) {
                rel_flux = fabs(sbss_rows[i].flux - sx_rows[best_j].flux) / fabs(sx_rows[best_j].flux);
            } else {
                rel_flux = fabs(sbss_rows[i].flux - sx_rows[best_j].flux);
            }

            sum_abs_dx += abs_dx;
            sum_abs_dy += abs_dy;
            sum_rel_flux += rel_flux;
        }
    }

    printf("SBSS rows: %zu\n", sbss_n);
    printf("SX rows:   %zu\n", sx_n);
    printf("Matches (<= %.1f px): %zu\n", max_match_distance, match_count);

    if (match_count == 0) {
        fprintf(stderr, "no matched sources found; comparison is not meaningful\n");
        return 1;
    }

    printf("Mean |dx|: %.4f px\n", sum_abs_dx / (double)match_count);
    printf("Mean |dy|: %.4f px\n", sum_abs_dy / (double)match_count);
    printf("Mean relative flux error: %.6f\n", sum_rel_flux / (double)match_count);

    return 0;
}
