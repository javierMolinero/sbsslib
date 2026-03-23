#ifndef SBSSLIB_SBSSLIB_H
#define SBSSLIB_SBSSLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sbss_detection {
    double x;
    double y;
    double peak;
    double flux;
    int area;
} sbss_detection;

typedef struct sbss_config {
    double detect_thresh_sigma;
    int detect_minarea;
    int filter_size;
    int max_sources;
} sbss_config;

#ifndef SBSS_MAX_IMAGE_PIXELS
#define SBSS_MAX_IMAGE_PIXELS (4096UL * 4096UL)
#endif

#ifndef SBSS_MAX_DETECTIONS
#define SBSS_MAX_DETECTIONS 50000UL
#endif

void sbss_config_set_defaults(sbss_config* cfg);

int sbss_config_load_file(
    const char* config_path,
    sbss_config* cfg,
    char* error_message,
    size_t error_message_size
);

int sbss_detect_from_fits(
    const char* fits_path,
    const sbss_config* cfg,
    sbss_detection* detections,
    size_t detections_capacity,
    size_t* detection_count,
    char* error_message,
    size_t error_message_size
);

int sbss_write_catalog_csv(
    const char* output_path,
    const sbss_detection* detections,
    size_t detection_count,
    char* error_message,
    size_t error_message_size
);

#ifdef __cplusplus
}
#endif

#endif
