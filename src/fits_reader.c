#include "internal.h"

#ifdef SBSSLIB_HAVE_CFITSIO
#include <fitsio.h>
#endif

int sbss_read_fits_image(
    const char* fits_path,
    double* pixels,
    size_t pixel_capacity,
    long* width,
    long* height,
    char* error_message,
    size_t error_message_size
) {
#ifndef SBSSLIB_HAVE_CFITSIO
    (void)fits_path;
    (void)pixels;
    (void)pixel_capacity;
    (void)width;
    (void)height;
    sbss_set_error(
        error_message,
        error_message_size,
        "sbsslib was built without CFITSIO support"
    );
    return -1;
#else
    fitsfile* fptr = NULL;
    int status = 0;
    int bitpix = 0;
    int naxis = 0;
    long naxes[2] = {0, 0};
    long fpixel[2] = {1, 1};
    long image_size = 0;

    if (pixels == NULL || width == NULL || height == NULL) {
        sbss_set_error(error_message, error_message_size, "invalid FITS output buffers");
        return -1;
    }

    if (fits_open_file(&fptr, fits_path, READONLY, &status) != 0) {
        char cfitsio_error[256] = {0};
        fits_get_errstatus(status, cfitsio_error);
        sbss_set_error(error_message, error_message_size, "cannot open FITS file %s: %s", fits_path, cfitsio_error);
        return -1;
    }

    if (fits_get_img_param(fptr, 2, &bitpix, &naxis, naxes, &status) != 0 || naxis != 2) {
        char cfitsio_error[256] = {0};
        fits_get_errstatus(status, cfitsio_error);
        sbss_set_error(error_message, error_message_size, "invalid FITS image dimensions: %s", cfitsio_error);
        fits_close_file(fptr, &status);
        return -1;
    }

    *width = naxes[0];
    *height = naxes[1];
    image_size = (*width) * (*height);

    if (image_size <= 0) {
        sbss_set_error(error_message, error_message_size, "empty FITS image");
        fits_close_file(fptr, &status);
        return -1;
    }

    if ((size_t)image_size > pixel_capacity) {
        sbss_set_error(
            error_message,
            error_message_size,
            "image pixel count exceeds configured capacity (%ld > %zu)",
            image_size,
            pixel_capacity
        );
        fits_close_file(fptr, &status);
        return -1;
    }

    if (fits_read_pix(fptr, TDOUBLE, fpixel, image_size, NULL, pixels, NULL, &status) != 0) {
        char cfitsio_error[256] = {0};
        fits_get_errstatus(status, cfitsio_error);
        sbss_set_error(error_message, error_message_size, "unable to read FITS image pixels: %s", cfitsio_error);
        fits_close_file(fptr, &status);
        return -1;
    }

    fits_close_file(fptr, &status);
    return 0;
#endif
}
