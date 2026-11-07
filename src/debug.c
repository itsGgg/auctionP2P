#include "../include/debug.h"
#include "../include/safe_output.h"
#include "../include/utils.h"

#include <stdarg.h>
#include <string.h>

void log_message(log_level_t level, const char *file, int line, const char *fmt, ...) {

    if (LOG_TRIGGER == LOG_UNIQ) {
        if (level != LOG_LEVEL)
            return;
    } else // LOG_TRIGGER == LOG_MAX
    {
        if (level > LOG_LEVEL)
            return;
    }

    const char *level_str = "UNKNOWN";
    const char *level_color = "";

    switch (level) {
        case LOG_INFO:
            level_str = "INFO";
            level_color = COLOR_INFO;
            break;
        case LOG_WARNING:
            level_str = "WARNING";
            level_color = COLOR_WARNING;
            break;
        case LOG_ERROR:
            level_str = "ERROR";
            level_color = COLOR_ERROR;
            break;
        default:
            break;
    }

    char buffer[LOG_MAX_BUF_SIZE];
    va_list args;
    va_start(args, fmt);

    int len = 0;
    if (strlen(fmt) != 0) {
        // Format final : [INFO] fichier.c:42: <message>
        len = snprintf(buffer, sizeof(buffer), "%s[%s]%s %s:%d: ", level_color, level_str,
                       COLOR_RESET, file, line);
    }

    if (len < 0 || (size_t)len >= sizeof(buffer)) {
        safe_printf("[LOG TRUNCATED BEFORE MSG]\n");
        va_end(args);
        return;
    }

    int msg_len = vsnprintf(buffer + len, sizeof(buffer) - len, fmt, args);
    va_end(args);

    if (msg_len < 0 || (size_t)(len + msg_len) >= sizeof(buffer)) {
        safe_printf("[LOG TRUNCATED]\n");
    } else {
        buffer[len + msg_len] = '\n';
        buffer[len + msg_len + 1] = '\0';

        safe_printf(buffer);
    }
}