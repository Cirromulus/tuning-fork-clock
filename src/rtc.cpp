// WIP, might delete later IDK

#include <pico/stdlib.h>
#include <hardware/rtc.h>
// #include "pico/util/datetime.h"

#include <cstdio>

static datetime_t start = {
    .year  = 0,
    .month = 01,
    .day   = 01,
    .dotw  = 0, // 0 is Sunday, so 3 is Wednesday
    .hour  = 0,
    .min   = 0,
    .sec   = 0
};

static volatile bool fired = false;

static void alarm_callback(void)
{
    // Lol, race conditions
    fired = true;
}

static void start_rtc(void)
{
    // Start the RTC
    rtc_init();

    // Alarm once a ... second?
    datetime_t alarm = {
        .year  = -1,
        .month = -1,
        .day   = -1,
        .dotw  = -1,
        .hour  = -1,
        .min   = -1,
        .sec   = 00
    };

    rtc_set_alarm(&alarm, &alarm_callback);

    while(true)
    {
        static long i = 0;
        printf("Dei' Mudda schwitzt beim Kaggne %d\r\n", i++);
        sleep_ms(500);
        if (fired)
        {
            printf("Schon seit einiger Zeit\r\n");
            fired = false;
        }
    }
}
