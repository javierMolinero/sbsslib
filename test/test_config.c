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

    if (cfg.filter_size < 1 || cfg.filter_size % 2 == 0) {
        fprintf(stderr, "default filter size should be odd and >= 1\n");
        return 1;
    }

    return 0;
}
