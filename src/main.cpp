#include "config.hpp"
#include "averager.hpp"

#include <pico/stdlib.h>
#include <pico/util/queue.h>
// #include <pico/multicore.h>

#include <stdio.h>
#include <cinttypes>   // uhg, oldschool
#include <array>
// #include <atomic>    -latomic not found?



bool timer_callback(repeating_timer_t *rt);

void osc_callback(uint gpio, uint32_t events);

// --------------

// ugh, globals
static OscCount oscCount = 0;
queue_t sample_fifo;

// Note: This whole timer thing should be replaced by an interrupt
// from an external clock of sufficient accuracy
repeating_timer_t sample_timer;


// --------------

int main() {
    setup_default_uart();
    stdio_init_all();

    queue_init(&sample_fifo, sizeof(OscCount), fifoSize);

    gpio_init(GPIO_WATCH_PIN);
    gpio_set_pulls(GPIO_WATCH_PIN, false, true);    // "Weak" pulldown
    gpio_set_irq_enabled_with_callback(GPIO_WATCH_PIN, GPIO_IRQ_EDGE_RISE, true, &osc_callback);

    printf ("Period [us], Frequency [Hz]\n");
    while(true)
    {
        OscCount oscPeriod = 0;
        queue_remove_blocking(&sample_fifo, &oscPeriod);

        printf("%lu,%f\n", oscPeriod, static_cast<double>(1000 * 1000) / oscPeriod);
    }

    return 0;
}

void osc_callback(uint gpio, uint32_t events)
{
    // static_assert(decltype(declval(time_us_32())) == OscCount);
    const OscCount now = time_us_32();
    const OscCount diff = now - oscCount;
    oscCount = now;
    if (!queue_try_add(&sample_fifo, &diff)) {
        printf("FIFO was full\n");
    };
}