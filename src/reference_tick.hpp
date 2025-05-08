#pragma once

#include <include/config.hpp>

#include "hardware/pio.h"
#include "hardware/dma.h"
// #include "hardware/clocks.h"
#include "pulsecounter.pio.h"

namespace clocksource
{


struct Internal
{

static AbsTime
getCurrentReferenceTicks();

static AbsTime
getTimeSinceReferenceStable_us();

};


template <AbsTime referenceClockFrequency>
struct External
{
    // TODO: Make that a parameter.
    PIO pio = pio1;
    uint sm = 0;
    int dmaChannel;

    uint32_t counterLow;
    uint32_t counterHigh;

    void
    counterHasWrapped()
    {
        counterHigh++;
    }

public:
    External(unsigned pin)
    {
        const int offset = pio_add_program(pio, &pulsecounter_program);
        pulsecounter_program_init(pio, sm, offset, pin);

        dmaChannel = dma_claim_unused_channel(true);

        dma_channel_config dc = dma_channel_get_default_config(dmaChannel);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, false);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));
        dma_channel_configure(dmaChannel, &dc,
            &counterLow,
            &pio->rxf[sm],
            1, // one transfer per data request
            true
        );

        // Find a free irq
        int pio_irq = pio_get_irq_num(pio, 0);
        if (irq_get_exclusive_handler(pio_irq)) {
            pio_irq++;
            if (irq_get_exclusive_handler(pio_irq)) {
                panic("All IRQs are in use");
            }
        }

        irq_add_shared_handler(pio_irq, std::bind(this, External::counterHasWrapped), PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY); // Add a shared IRQ handler
        irq_set_enabled(pio_irq, true); // Enable the IRQ
        const uint irq_index = pio_irq - pio_get_irq_num(pio, 0); // Get index of the IRQ
        pio_set_irqn_source_enabled(pio, irq_index, pio_interrupt_source::pis_interrupt0, true);
    }

    AbsTime
    getCurrentReferenceTicks()
    {
        return counterLow | counterHigh << 32;
    }

    AbsTime
    getTimeSinceReferenceStable_us()
    {
        static constexpr AbsTime multiplicator = referenceClockFrequency / 1'000'000;
        static_assert (multiplicator * 1'000'000 == referenceClockFrequency,
                        "ReferenceClock not divisible by microseconds without loss");
        getCurrentReferenceTicks() / multiplicator;
    }
};

} // namespace clocksource