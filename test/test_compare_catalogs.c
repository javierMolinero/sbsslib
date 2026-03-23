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
    double sum_abs_dxmin = 0.0;
    double sum_abs_dxmax = 0.0;
    double sum_abs_dymin = 0.0;
    double sum_abs_dymax = 0.0;
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
            sum_abs_dxmin += fabs((double)sbss_rows[i].xmin - (double)sx_rows[best_j].xmin);
            sum_abs_dxmax += fabs((double)sbss_rows[i].xmax - (double)sx_rows[best_j].xmax);
            sum_abs_dymin += fabs((double)sbss_rows[i].ymin - (double)sx_rows[best_j].ymin);
            sum_abs_dymax += fabs((double)sbss_rows[i].ymax - (double)sx_rows[best_j].ymax);
        }
    }

    printf("\n");
    printf("====================================\n");
    printf("  SBSSLIB vs SEXTRACTOR COMPARISON\n");
    printf("====================================\n");
    printf("\n");
    printf("Detection Summary:\n");
    printf("  SBSS detections:  %zu\n", sbss_n);
    printf("  SX detections:    %zu\n", sx_n);
    printf("  Matched (<= %.1f px dist):  %zu\n", max_match_distance, match_count);

    if (match_count == 0) {
        fprintf(stderr, "no matched sources found; comparison is not meaningful\n");
        return 1;
    }

    {
        double mean_dx = sum_abs_dx / (double)match_count;
        double mean_dy = sum_abs_dy / (double)match_count;
        double mean_pos_error = (mean_dx + mean_dy) / 2.0;
        double mean_flux_rel = sum_rel_flux / (double)match_count;
        double mean_dxmin = sum_abs_dxmin / (double)match_count;
        double mean_dxmax = sum_abs_dxmax / (double)match_count;
        double mean_dymin = sum_abs_dymin / (double)match_count;
        double mean_dymax = sum_abs_dymax / (double)match_count;

        /* Calculate similarity percentages */
        double detection_recall = (match_count / (double)sx_n) * 100.0;
        double pos_tolerance = 0.5;  /* target <= 0.5 px */
        double pos_accuracy = 100.0 * (1.0 - (mean_pos_error / pos_tolerance));
        if (pos_accuracy < 0.0) pos_accuracy = 0.0;
        if (pos_accuracy > 100.0) pos_accuracy = 100.0;

        double flux_tolerance = 0.15;  /* target <= 15% error */
        double flux_accuracy = 100.0 * (1.0 - (mean_flux_rel / flux_tolerance));
        if (flux_accuracy < 0.0) flux_accuracy = 0.0;
        if (flux_accuracy > 100.0) flux_accuracy = 100.0;

        double bbox_error = (mean_dxmin + mean_dxmax + mean_dymin + mean_dymax) / 4.0;
        double bbox_tolerance = 0.3;
        double bbox_accuracy = 100.0 * (1.0 - (bbox_error / bbox_tolerance));
        if (bbox_accuracy < 0.0) bbox_accuracy = 0.0;
        if (bbox_accuracy > 100.0) bbox_accuracy = 100.0;

        double overall_similarity = (detection_recall * 0.4 + pos_accuracy * 0.3 + 
                                     flux_accuracy * 0.2 + bbox_accuracy * 0.1) / 100.0;

        printf("\n");
        printf("Detailed Metrics:\n");
        printf("  Mean |dx|:                %.4f px\n", mean_dx);
        printf("  Mean |dy|:                %.4f px\n", mean_dy);
        printf("  Mean position error:      %.4f px\n", mean_pos_error);
        printf("  Mean relative flux error: %.4f (%.2f%%)\n", mean_flux_rel, mean_flux_rel * 100.0);
        printf("  Mean |dXMIN|:             %.4f px\n", mean_dxmin);
        printf("  Mean |dXMAX|:             %.4f px\n", mean_dxmax);
        printf("  Mean |dYMIN|:             %.4f px\n", mean_dymin);
        printf("  Mean |dYMAX|:             %.4f px\n", mean_dymax);
        printf("  Mean bbox error:          %.4f px\n", bbox_error);

        printf("\n");
        printf("Similarity Metrics:\n");
        printf("  Detection Recall:         %.2f%% (%zu/%zu)\n", detection_recall, match_count, sx_n);
        printf("  Position Accuracy:        %.2f%%\n", pos_accuracy);
        printf("  Flux Accuracy:            %.2f%%\n", flux_accuracy);
        printf("  Bbox Accuracy:            %.2f%%\n", bbox_accuracy);
        printf("\n");
        printf("  >>> OVERALL SIMILARITY:   %.2f%% <<<\n", overall_similarity * 100.0);
        printf("\n");

        if (mean_dx > 0.5 || mean_dy > 0.5) {
            fprintf(stderr, "⚠ Warning: mean position error exceeds 0.5 px target\n");
            return 1;
        }
    }

    return 0;
}
