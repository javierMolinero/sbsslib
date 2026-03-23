#include <stdio.h>

#include "sbsslib/sbsslib.h"

int main(void) {
    sbss_config cfg;

    sbss_config_set_defaults(&cfg);

    if (cfg.detect_thresh_sigma <= 0.0) {
        fprintf(stderr, "default threshold sigma should be positive\n");
        return 1;
    }

    if (cfg.detect_minarea < 1) {
        fprintf(stderr, "default min area should be >= 1\n");
        return 1;
    }

    if (cfg.deblend_nthresh < 1) {
        fprintf(stderr, "default DEBLEND_NTHRESH should be >= 1\n");
        return 1;
    }

    if (cfg.deblend_mincont < 0.0 || cfg.deblend_mincont > 1.0) {
        fprintf(stderr, "default DEBLEND_MINCONT should be in [0,1]\n");
        return 1;
    }

    if (cfg.filter_enabled == 0) {
        fprintf(stderr, "default filter should be enabled\n");
        return 1;
    }

    if (cfg.filter_size < 1 || cfg.filter_size % 2 == 0) {
        fprintf(stderr, "default filter size should be odd and >= 1\n");
        return 1;
    }

    if (cfg.filter_kernel_size < 1 || cfg.filter_kernel_size % 2 == 0) {
        fprintf(stderr, "default filter kernel size should be odd and >= 1\n");
        return 1;
    }

    if (cfg.back_size < 8) {
        fprintf(stderr, "default BACK_SIZE should be >= 8\n");
        return 1;
    }

    if (cfg.back_filtersize < 1 || cfg.back_filtersize % 2 == 0) {
        fprintf(stderr, "default BACK_FILTERSIZE should be odd and >= 1\n");
        return 1;
    }

    return 0;
}
