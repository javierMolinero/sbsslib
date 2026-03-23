#include <stdio.h>

#include "internal.h"

int sbss_write_catalog_csv(
    const char* output_path,
    const sbss_detection* detections,
    size_t detection_count,
    char* error_message,
    size_t error_message_size
) {
    FILE* out;
    size_t i;

    if (output_path == NULL || detections == NULL) {
        sbss_set_error(error_message, error_message_size, "invalid catalog output input");
        return -1;
    }

    out = fopen(output_path, "w");
    if (out == NULL) {
        sbss_set_error(error_message, error_message_size, "unable to open output catalog: %s", output_path);
        return -1;
    }

    fprintf(out, "id,x,y,peak,flux,area\n");
    for (i = 0; i < detection_count; ++i) {
        fprintf(
            out,
            "%zu,%.6f,%.6f,%.6f,%.6f,%d\n",
            i + 1,
            detections[i].x,
            detections[i].y,
            detections[i].peak,
            detections[i].flux,
            detections[i].area
        );
    }

    fclose(out);
    return 0;
}
