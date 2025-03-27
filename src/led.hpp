#pragma once

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}

class OnboardLED
{

    PIO pio = pio0;
    uint sm = 0;
    int offset;

public:
    OnboardLED(unsigned pin)
    : offset{pio_add_program(pio, &ws2812_program)}
    {
        ws2812_program_init(pio, sm, offset, pin, 800000, false /* not rgbw */);
    }


    void put_pixel(uint32_t pixel_grb) {
        pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
    }
};

class Status
{
    OnboardLED& led;
    uint8_t brightness;
public:
    Status(OnboardLED& led, uint8_t brightness = 0x20)
        : led{led}, brightness{brightness}
    {
        led.put_pixel(0);
    }

    void
    expectedFrequency()
    {
        led.put_pixel(urgb_u32(0, brightness, 0));
    }

    void
    tooHighFrequency()
    {
        led.put_pixel(urgb_u32(0, brightness/2, brightness));
    }

    void
    tooLowFrequency()
    {
        led.put_pixel(urgb_u32(brightness, brightness/2, 0));
    }

    void
    noSignal()
    {
        led.put_pixel(urgb_u32(brightness, 0, brightness / 2));
    }
};