#ifndef SBSSLIB_INTERNAL_H
#define SBSSLIB_INTERNAL_H

#include <stddef.h>

#include "sbsslib/sbsslib.h"

int sbss_read_fits_image(
    const char* fits_path,
    double* pixels,
    size_t pixel_capacity,
    long* width,
    long* height,
    char* error_message,
    size_t error_message_size
);

void sbss_set_error(char* buffer, size_t buffer_size, const char* fmt, ...);

#endif
