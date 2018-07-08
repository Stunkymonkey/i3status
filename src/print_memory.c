// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

#define STRING_SIZE 10

#define BINARY_BASE UINT64_C(1024)
#define MAX_EXPONENT 4
static const char *const iec_symbols[MAX_EXPONENT + 1] = {"", "Ki", "Mi", "Gi", "Ti"};

static const char memoryfile_linux[] = "/proc/meminfo";

/*
 * Prints the given amount of bytes in a human readable manner.
 *
 */
static void print_bytes_human(char *outwalk, uint64_t bytes) {
    double size = bytes;
    int exponent = 0;
    int bin_base = BINARY_BASE;
    while (size >= bin_base && exponent < MAX_EXPONENT) {
        size /= bin_base;
        exponent += 1;
    }
    snprintf(outwalk, STRING_SIZE, "%.1f %sB", size, iec_symbols[exponent]);
}

static void print_percentage(char *outwalk, float percent) {
    snprintf(outwalk, STRING_SIZE, "%.1f%s", percent, pct_mark);
}

/*
 * Convert a string to its absolute representation based on the total
 * memory of `mem_total`.
 *
 * The string can contain any percentage values, which then return a
 * the value of `size` in relation to `mem_total`.
 * Alternatively an absolute value can be given, suffixed with an iec
 * symbol.
 *
 */
static long memory_absolute(const long mem_total, const char *size) {
    char *endptr = NULL;

    long mem_absolute = strtol(size, &endptr, 10);

    if (endptr) {
        while (endptr[0] != '\0' && isspace(endptr[0]))
            endptr++;

        switch (endptr[0]) {
            case 'T':
            case 't':
                mem_absolute *= BINARY_BASE;
            case 'G':
            case 'g':
                mem_absolute *= BINARY_BASE;
            case 'M':
            case 'm':
                mem_absolute *= BINARY_BASE;
            case 'K':
            case 'k':
                mem_absolute *= BINARY_BASE;
                break;
            case '%':
                mem_absolute = mem_total * mem_absolute / 100;
                break;
            default:
                break;
        }
    }

    return mem_absolute;
}

void print_memory(yajl_gen json_gen, char *buffer, const char *format, const char *format_degraded, const char *threshold_degraded, const char *threshold_critical, const char *memory_used_method) {
    char *outwalk = buffer;

#if defined(linux)
    const char *selected_format = format;
    const char *output_color = NULL;

    long ram_total = -1;
    long ram_free = -1;
    long ram_available = -1;
    long ram_used = -1;
    long ram_shared = -1;
    long ram_cached = -1;
    long ram_buffers = -1;

    FILE *file = fopen(memoryfile_linux, "r");
    if (!file) {
        goto error;
    }
    char line[128];
    while (fgets(line, sizeof line, file)) {
        if (BEGINS_WITH(line, "MemTotal:")) {
            ram_total = strtol(line + strlen("MemTotal:"), NULL, 10);
        }
        if (BEGINS_WITH(line, "MemFree:")) {
            ram_free = strtol(line + strlen("MemFree:"), NULL, 10);
        }
        if (BEGINS_WITH(line, "MemAvailable:")) {
            ram_available = strtol(line + strlen("MemAvailable:"), NULL, 10);
        }
        if (BEGINS_WITH(line, "Buffers:")) {
            ram_buffers = strtol(line + strlen("Buffers:"), NULL, 10);
        }
        if (BEGINS_WITH(line, "Cached:")) {
            ram_cached = strtol(line + strlen("Cached:"), NULL, 10);
        }
        if (BEGINS_WITH(line, "Shmem:")) {
            ram_shared = strtol(line + strlen("Shmem:"), NULL, 10);
        }
        if (ram_total != -1 && ram_free != -1 && ram_available != -1 && ram_buffers != -1 && ram_cached != -1 && ram_shared != -1) {
            break;
        }
    }
    fclose(file);

    if (ram_total == -1 || ram_free == -1 || ram_available == -1 || ram_buffers == -1 || ram_cached == -1 || ram_shared == -1) {
        goto error;
    }

    ram_total = ram_total * BINARY_BASE;
    ram_free = ram_free * BINARY_BASE;
    ram_available = ram_available * BINARY_BASE;
    ram_buffers = ram_buffers * BINARY_BASE;
    ram_cached = ram_cached * BINARY_BASE;
    ram_shared = ram_shared * BINARY_BASE;

    if (BEGINS_WITH(memory_used_method, "memavailable")) {
        ram_used = ram_total - ram_available;
    } else if (BEGINS_WITH(memory_used_method, "classical")) {
        ram_used = ram_total - ram_free - ram_buffers - ram_cached;
    }

    if (threshold_degraded) {
        long abs = memory_absolute(ram_total, threshold_degraded);
        if (ram_available < abs) {
            output_color = "color_degraded";
        }
    }

    if (threshold_critical) {
        long abs = memory_absolute(ram_total, threshold_critical);
        if (ram_available < abs) {
            output_color = "color_bad";
        }
    }

    if (output_color) {
        START_COLOR(output_color);

        if (format_degraded)
            selected_format = format_degraded;
    }

    char string_ram_total[STRING_SIZE];
    char string_ram_used[STRING_SIZE];
    char string_ram_free[STRING_SIZE];
    char string_ram_available[STRING_SIZE];
    char string_ram_shared[STRING_SIZE];
    char string_percentage_free[STRING_SIZE];
    char string_percentage_available[STRING_SIZE];
    char string_percentage_used[STRING_SIZE];
    char string_percentage_shared[STRING_SIZE];

    print_bytes_human(string_ram_total, ram_total);
    print_bytes_human(string_ram_used, ram_used);
    print_bytes_human(string_ram_free, ram_free);
    print_bytes_human(string_ram_available, ram_available);
    print_bytes_human(string_ram_shared, ram_shared);
    print_percentage(string_percentage_free, 100.0 * ram_free / ram_total);
    print_percentage(string_percentage_available, 100.0 * ram_available / ram_total);
    print_percentage(string_percentage_used, 100.0 * ram_used / ram_total);
    print_percentage(string_percentage_shared, 100.0 * ram_shared / ram_total);

    placeholder_t placeholders[] = {
        {.name = "%total", .value = string_ram_total},
        {.name = "%used", .value = string_ram_used},
        {.name = "%free", .value = string_ram_free},
        {.name = "%available", .value = string_ram_available},
        {.name = "%shared", .value = string_ram_shared},
        {.name = "%percentage_free", .value = string_percentage_free},
        {.name = "%percentage_available", .value = string_percentage_available},
        {.name = "%percentage_used", .value = string_percentage_used},
        {.name = "%percentage_shared", .value = string_percentage_shared}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    buffer = format_placeholders(selected_format, &placeholders[0], num);

    if (output_color)
        END_COLOR;
    OUTPUT_FULL_TEXT(buffer);

    return;
error:
    OUTPUT_FULL_TEXT("can't read memory");
    fputs("i3status: Cannot read system memory using /proc/meminfo\n", stderr);
#else
    OUTPUT_FULL_TEXT("");
    fputs("i3status: Memory status information is not supported on this system\n", stderr);
#endif
}
