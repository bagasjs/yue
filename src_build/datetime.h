#ifndef DATETIME_H_
#define DATETIME_H_

#include <stdbool.h>
#include <assert.h>

typedef struct Datetime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} Datetime;

Datetime datetime_now(void);
Datetime datetime_add(Datetime a, Datetime b);
int datetime_get_max_days_in_month(int year, int month);
// +1Y1M1w1d1h1m1s -> 0001-01-01 01:01:01
bool datetime_parse_duration(const char *source, Datetime *dt);
int datetime_cmp(Datetime a, Datetime b);

#endif // DATETIME_H_

#ifdef DATETIME_IMPLEMENTATION
#undef DATETIME_IMPLEMENTATION
#include <time.h>

Datetime datetime_now(void)
{
    time_t rawtime;
    struct tm *tm;
    time(&rawtime);
    tm = localtime(&rawtime);

    return (Datetime) {
        .year  = 1900 + tm->tm_year,
        .day   = tm->tm_mday,
        .month = tm->tm_mon + 1,
        .hour  = tm->tm_hour,
        .minute = tm->tm_min,
        .second = tm->tm_sec,
    };
}

int datetime_get_max_days_in_month(int year, int month)
{
    switch(month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            return year % 4 == 0 ? 29 : 28;
        default:
            return -1;
    }
}

void divmod_add(int *divs, int *mods, int a, int b, int max)
{
    *divs = (a + b) / max;
    *mods = (a + b) % max;
}

Datetime datetime_add(Datetime a, Datetime b)
{
    Datetime result = {0};
    int total = 0;
    result.year = a.year + b.year;
    divmod_add(&result.minute, &result.second, a.second, b.second, 60);
    divmod_add(&result.hour, &result.minute, a.minute, b.minute, 60);
    divmod_add(&result.day, &result.hour, a.hour, b.hour, 24);
    result.day = a.day + b.day;
    result.month = a.month + b.month;
    int current_threshold = datetime_get_max_days_in_month(result.year, result.month);
    while(result.day > current_threshold) {
        result.day -= current_threshold;
        result.month += 1;
        if(result.month > 12) {
            result.month = 1;
            result.year += 1;
        }
    }
    return result;
}

int datetime_cmp(Datetime a, Datetime b)
{
    int x = 0;
    x = a.year - b.year;
    if(x != 0) return x;
    x = a.month - b.month;
    if(x != 0) return x;
    x = a.day - b.day;
    if(x != 0) return x;
    x = a.hour - b.hour;
    if(x != 0) return x;
    x = a.minute - b.minute;
    if(x != 0) return x;
    x = a.second - b.second;
    return x;
}

bool datetime_parse_duration(const char *source, Datetime *dt)
{
    // +1Y1M1w1d1h1m1s -> if now = 2020-01-24 10:00:00:000 then it becomes 2021-02-25 11:01:01
    if(source[0] != '+') return false;
    int n = 0;
    Datetime add = {0};
    for(int i = 0; source[i] != 0 && i < 256; ++i) {
        while('0' <= source[i] && source[i] <= '9') {
            n = n*10 + (source[i] - '0');
            i+=1;
        }
        switch(source[i]) {
            case 'Y':
                add.year  += n;
                break;
            case 'M':
                add.month += n;
            case 'w':
                add.day += 7*n;
                break;
            case 'd':
                add.day += n;
                break;
            case 'h':
                add.hour += n;
                break;
            case 'm':
                add.minute += n;
                break;
            case 's':
                add.second += n;
                break;
            default:
                assert(0 && "invalid deadline syntax");
        }
        n = 0;
    }
    *dt = add;
    return true;
}

#endif // DATETIME_IMPLEMENTATION
