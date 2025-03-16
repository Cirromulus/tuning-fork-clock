#include "config.hpp"

#include <pico/stdlib.h>
#include <pico/util/queue.h>
// #include <pico/multicore.h>

#include <stdio.h>
#include <cinttypes>   // uhg, oldschool
// #include <atomic>    -latomic not found?

bool timer_callback(repeating_timer_t *rt);

void osc_callback(uint gpio, uint32_t events);

static constexpr int64_t
freqToPeriodUs(uint64_t frequency)
{
    // negative timeout means exact delay (rather than delay between callbacks)
    return -1000000 / frequency;
}


// --------------

// ugh, globals
static /*std::atomic<*/OscCount oscCount;
queue_t sample_fifo;

// Note: This whole timer thing should be replaced by an interrupt
// from an external clock of sufficient accuracy
repeating_timer_t sample_timer;


// --------------

int main() {
    setup_default_uart();
    stdio_init_all();

    queue_init(&sample_fifo, sizeof(OscCount), fifoSize);

    // todo: Setup interrupt
    oscCount = 99;

    // negative timeout means exact delay (rather than delay between callbacks)
    if (!add_repeating_timer_us(freqToPeriodUs(sampleFreq), timer_callback, NULL, &sample_timer))
    {
        printf("Failed to add sampling timer\n");
        return 1;
    }

    gpio_init(GPIO_WATCH_PIN);
    gpio_set_pulls(GPIO_WATCH_PIN, false, true);    // "Weak" pulldown
    gpio_set_irq_enabled_with_callback(GPIO_WATCH_PIN, GPIO_IRQ_EDGE_RISE, true, &osc_callback);

    // TODO:  Drop first sample as it may be the incorrect duration

    while(true)
    {
        OscCount localCopy = 0;
        queue_remove_blocking(&sample_fifo, &localCopy);

        printf("got oscillator count: %llu\n", localCopy);
    }

    return 0;
}


bool timer_callback(__unused repeating_timer_t *rt) {
    if (!queue_try_add(&sample_fifo, &oscCount)) {
        printf("FIFO was full\n");
    }
    oscCount = 0;

    return true; // keep repeating
}


void osc_callback(uint gpio, uint32_t events)
{
    // TODO: timer should have a higher Priority as osc_callback :D
    oscCount++;
}