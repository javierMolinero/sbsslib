#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

#include "internal.h"

static void trim_in_place(char* text) {
    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }

    size_t start = 0;
    while (text[start] != '\0' && isspace((unsigned char)text[start])) {
        start++;
    }

    if (start > 0) {
        memmove(text, text + start, len - start + 1);
    }
}

void sbss_set_error(char* buffer, size_t buffer_size, const char* fmt, ...) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buffer, buffer_size, fmt, args);
    va_end(args);
}

void sbss_config_set_defaults(sbss_config* cfg) {
    if (cfg == NULL) {
        return;
    }

    cfg->detect_thresh_sigma = 3.0;
    cfg->detect_minarea = 5;
    cfg->filter_size = 3;
    cfg->max_sources = (int)SBSS_MAX_DETECTIONS;
}

static int parse_int(const char* value, int* out_value) {
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return 0;
    }
    *out_value = (int)parsed;
    return 1;
}

static int parse_double(const char* value, double* out_value) {
    char* end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || *end != '\0') {
        return 0;
    }
    *out_value = parsed;
    return 1;
}

static int sbss_stricmp(const char* a, const char* b) {
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

int sbss_config_load_file(
    const char* config_path,
    sbss_config* cfg,
    char* error_message,
    size_t error_message_size
) {
    FILE* file = NULL;
    char line[512];
    int line_number = 0;

    if (cfg == NULL || config_path == NULL) {
        sbss_set_error(error_message, error_message_size, "invalid config input");
        return -1;
    }

    file = fopen(config_path, "r");
    if (file == NULL) {
        sbss_set_error(error_message, error_message_size, "unable to open config file: %s", config_path);
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char* key = NULL;
        char* value = NULL;
        char* equals = NULL;

        line_number++;
        trim_in_place(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        equals = strchr(line, '=');
        if (equals == NULL) {
            sbss_set_error(error_message, error_message_size, "config parse error at line %d", line_number);
            fclose(file);
            return -1;
        }

        *equals = '\0';
        key = line;
        value = equals + 1;

        trim_in_place(key);
        trim_in_place(value);

        if (sbss_stricmp(key, "DETECT_THRESH") == 0) {
            if (!parse_double(value, &cfg->detect_thresh_sigma) || cfg->detect_thresh_sigma <= 0.0) {
                sbss_set_error(error_message, error_message_size, "invalid DETECT_THRESH at line %d", line_number);
                fclose(file);
                return -1;
            }
        } else if (sbss_stricmp(key, "DETECT_MINAREA") == 0) {
            if (!parse_int(value, &cfg->detect_minarea) || cfg->detect_minarea < 1) {
                sbss_set_error(error_message, error_message_size, "invalid DETECT_MINAREA at line %d", line_number);
                fclose(file);
                return -1;
            }
        } else if (sbss_stricmp(key, "FILTER_SIZE") == 0) {
            if (!parse_int(value, &cfg->filter_size) || cfg->filter_size < 1 || cfg->filter_size % 2 == 0) {
                sbss_set_error(error_message, error_message_size, "invalid FILTER_SIZE at line %d", line_number);
                fclose(file);
                return -1;
            }
        } else if (sbss_stricmp(key, "MAX_SOURCES") == 0) {
            if (!parse_int(value, &cfg->max_sources) || cfg->max_sources < 1 || (size_t)cfg->max_sources > SBSS_MAX_DETECTIONS) {
                sbss_set_error(error_message, error_message_size, "invalid MAX_SOURCES at line %d", line_number);
                fclose(file);
                return -1;
            }
        }
    }

    fclose(file);
    return 0;
}
