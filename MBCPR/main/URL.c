#include "URL.h"
#include <string.h>
#include <ctype.h>

static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return 0;
}

void url_decode(char *str) {
    char *p_in = str;
    char *p_out = str;
    char hex[3] = {0};

    while (*p_in) {
        if (*p_in == '%') {
            if (*(p_in + 1) && *(p_in + 2)) {
                hex[0] = *(p_in + 1);
                hex[1] = *(p_in + 2);
                *p_out = (char)((hex_to_int(hex[0]) << 4) | hex_to_int(hex[1]));
                p_in += 2;
            } else {
                *p_out = *p_in;
            }
        } else if (*p_in == '+') {
            *p_out = *p_in;
        } else {
            *p_out = *p_in;
        }

        p_in++;
        p_out++;
    }
    *p_out = '\0';
}