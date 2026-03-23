#include <stdio.h>

#include "internal.h"

int sbss_write_catalog_sbss(
    const char* output_path,
    const sbss_detection* detections,
    size_t detection_count,
    char* error_message,
    size_t error_message_size
) {
    FILE* out;
    size_t i;
    double x_image;
    double y_image;

    if (output_path == NULL || detections == NULL) {
        sbss_set_error(error_message, error_message_size, "invalid catalog output input");
        return -1;
    }

    out = fopen(output_path, "w");
    if (out == NULL) {
        sbss_set_error(error_message, error_message_size, "unable to open output catalog: %s", output_path);
        return -1;
    }

    for (i = 0; i < detection_count; ++i) {
        /* SExtractor-like pixel coordinates are 1-based. */
        x_image = detections[i].x + 1.0;
        y_image = detections[i].y + 1.0;

        fprintf(
            out,
            "%10.3f %10.3f %10d %10d %10d %10d %12.6g\n",
            x_image,
            y_image,
            detections[i].xmin + 1,
            detections[i].xmax + 1,
            detections[i].ymin + 1,
            detections[i].ymax + 1,
            detections[i].flux
        );
    }

    fclose(out);
    return 0;
}
