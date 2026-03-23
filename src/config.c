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
    cfg->back_size = 64;
    cfg->back_filtersize = 3;
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

static int parse_first_int_token(const char* value, int* out_value) {
    char token[64];
    size_t i = 0;

    while (value[i] != '\0' && value[i] != ',' && !isspace((unsigned char)value[i]) && i < (sizeof(token) - 1U)) {
        token[i] = value[i];
        i++;
    }

    token[i] = '\0';

    if (token[0] == '\0') {
        return 0;
    }

    return parse_int(token, out_value);
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
    int detect_thresh_set = 0;

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
        char* comment = NULL;
        int disable_filter = 0;

        line_number++;
        trim_in_place(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
            trim_in_place(line);
        }

        if (line[0] == '\0') {
            continue;
        }

        equals = strchr(line, '=');
        if (equals != NULL) {
            *equals = '\0';
            key = line;
            value = equals + 1;
        } else {
            size_t i = 0;
            while (line[i] != '\0' && !isspace((unsigned char)line[i])) {
                i++;
            }

            if (line[i] == '\0') {
                /* Ignore single-token lines to stay compatible with permissive SExtractor config style. */
                continue;
            }

            line[i] = '\0';
            key = line;
            value = &line[i + 1];
        }

        trim_in_place(key);
        trim_in_place(value);

        if (value[0] == '\0') {
            continue;
        }

        if (sbss_stricmp(key, "DETECT_THRESH") == 0) {
            if (!parse_double(value, &cfg->detect_thresh_sigma) || cfg->detect_thresh_sigma <= 0.0) {
                sbss_set_error(error_message, error_message_size, "invalid DETECT_THRESH at line %d", line_number);
                fclose(file);
                return -1;
            }
            detect_thresh_set = 1;
        } else if (sbss_stricmp(key, "ANALYSIS_THRESH") == 0) {
            if (!detect_thresh_set) {
                if (!parse_double(value, &cfg->detect_thresh_sigma) || cfg->detect_thresh_sigma <= 0.0) {
                    sbss_set_error(error_message, error_message_size, "invalid ANALYSIS_THRESH at line %d", line_number);
                    fclose(file);
                    return -1;
                }
            }
        } else if (sbss_stricmp(key, "DETECT_MINAREA") == 0) {
            if (!parse_int(value, &cfg->detect_minarea) || cfg->detect_minarea < 1) {
                sbss_set_error(error_message, error_message_size, "invalid DETECT_MINAREA at line %d", line_number);
                fclose(file);
                return -1;
            }
        } else if (sbss_stricmp(key, "FILTER") == 0) {
            if (sbss_stricmp(value, "N") == 0 || sbss_stricmp(value, "NO") == 0) {
                disable_filter = 1;
            }
            if (disable_filter != 0) {
                cfg->filter_size = 1;
            }
        } else if (sbss_stricmp(key, "FILTER_SIZE") == 0) {
            if (!parse_int(value, &cfg->filter_size) || cfg->filter_size < 1 || cfg->filter_size % 2 == 0) {
                sbss_set_error(error_message, error_message_size, "invalid FILTER_SIZE at line %d", line_number);
                fclose(file);
                return -1;
            }
        } else if (sbss_stricmp(key, "BACK_SIZE") == 0) {
            if (!parse_first_int_token(value, &cfg->back_size) || cfg->back_size < 8) {
                sbss_set_error(error_message, error_message_size, "invalid BACK_SIZE at line %d", line_number);
                fclose(file);
                return -1;
            }
        } else if (sbss_stricmp(key, "BACK_FILTERSIZE") == 0) {
            if (!parse_first_int_token(value, &cfg->back_filtersize) || cfg->back_filtersize < 1) {
                sbss_set_error(error_message, error_message_size, "invalid BACK_FILTERSIZE at line %d", line_number);
                fclose(file);
                return -1;
            }
            if ((cfg->back_filtersize % 2) == 0) {
                cfg->back_filtersize += 1;
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
