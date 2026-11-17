#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* HTTP Date 固定使用 GMT，直接换算 UTC，不受界面的北京时间设置影响。 */
static inline time_t epw_http_date(const char *value)
{
    char weekday[4], month[4], zone[4];
    int day, year, hour, minute, second, end = 0;
    if (value == NULL || sscanf(value, "%3s, %d %3s %d %d:%d:%d %3s%n",
        weekday, &day, month, &year, &hour, &minute, &second, zone, &end) != 8 ||
        value[end] != '\0' || strcmp(zone, "GMT") != 0 || year < 2024 || year > 2099 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return 0;
    }
    const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int mon = 0;
    while (mon < 12 && strncmp(month, months + mon * 3, 3) != 0) {
        mon++;
    }
    if (mon == 12) {
        return 0;
    }
    static const int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    int max_day = before[mon + 1] - before[mon] + (mon == 1 && leap);
    if (day < 1 || day > max_day) {
        return 0;
    }
    long days = 365L * (year - 1970) + (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400
                - (1969 / 4 - 1969 / 100 + 1969 / 400);
    days += before[mon] + (mon > 1 && leap) + day - 1;
    return (time_t)days * 86400 + hour * 3600 + minute * 60 + second;
}
