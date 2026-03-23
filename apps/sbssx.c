#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sbsslib/sbsslib.h"

static sbss_detection g_detections[SBSS_MAX_DETECTIONS];

static void make_default_catalog_path(const char* fits_path, char* out_path, size_t out_size) {
    if (fits_path == NULL || out_path == NULL || out_size == 0U) {
        return;
    }

    (void)snprintf(out_path, out_size, "%s.sbss", fits_path);
}

static void print_usage(void) {
    printf("sbssx - lightweight source extractor focused on point detection\n");
    printf("Usage:\n");
    printf("  sbssx <image.fits> [-c <config.conf>] [-CATALOG_NAME <catalog.sbss>]\n");
    printf("                     [-DETECT_THRESH <sigma>] [-DETECT_MINAREA <n>]\n");
}

static int parse_int(const char* text, int* out) {
    char* end = NULL;
    long parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static int parse_double(const char* text, double* out) {
    char* end = NULL;
    double parsed = strtod(text, &end);
    if (end == text || *end != '\0') {
        return 0;
    }
    *out = parsed;
    return 1;
}

int main(int argc, char** argv) {
    const char* fits_path = NULL;
    const char* config_path = NULL;
    const char* catalog_path = NULL;
    char default_catalog_path[1024] = {0};
    sbss_config cfg;
    size_t detection_count = 0;
    char error_message[512] = {0};
    int i;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    sbss_config_set_defaults(&cfg);

    fits_path = argv[1];
    make_default_catalog_path(fits_path, default_catalog_path, sizeof(default_catalog_path));
    catalog_path = default_catalog_path;

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-CATALOG_NAME") == 0 && i + 1 < argc) {
            catalog_path = argv[++i];
        } else if (strcmp(argv[i], "-DETECT_THRESH") == 0 && i + 1 < argc) {
            if (!parse_double(argv[++i], &cfg.detect_thresh_sigma) || cfg.detect_thresh_sigma <= 0.0) {
                fprintf(stderr, "invalid -DETECT_THRESH value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-DETECT_MINAREA") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], &cfg.detect_minarea) || cfg.detect_minarea < 1) {
                fprintf(stderr, "invalid -DETECT_MINAREA value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (config_path != NULL) {
        if (sbss_config_load_file(config_path, &cfg, error_message, sizeof(error_message)) != 0) {
            fprintf(stderr, "config error: %s\n", error_message);
            return 1;
        }
    }

    if (sbss_detect_from_fits(
            fits_path,
            &cfg,
            g_detections,
            SBSS_MAX_DETECTIONS,
            &detection_count,
            error_message,
            sizeof(error_message)
        ) != 0) {
        fprintf(stderr, "detection failed: %s\n", error_message);
        return 1;
    }

    if (sbss_write_catalog_sbss(
            catalog_path,
            g_detections,
            detection_count,
            error_message,
            sizeof(error_message)
        ) != 0) {
        fprintf(stderr, "catalog write failed: %s\n", error_message);
        return 1;
    }

    printf("Detected %zu sources\n", detection_count);
    printf("Catalog written to %s\n", catalog_path);

    return 0;
}
