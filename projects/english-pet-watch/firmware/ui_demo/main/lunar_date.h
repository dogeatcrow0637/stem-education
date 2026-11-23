#pragma once
#include <stdbool.h>
#include <stddef.h>

/* 输入本地公历日期；无效或超出离线范围时返回 false 并清空输出。 */
bool lunar_date_format(int year, int month, int day, char *out, size_t size);
