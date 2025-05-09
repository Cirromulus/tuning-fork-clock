#pragma once

#include <include/config.hpp>

#include <stdio.h>      // Only for debug prints
#include <hardware/pio.h>
#include <hardware/dma.h>
// #include <hardware/clocks.h>
#include <pulsecounter.pio.h>

#include <optional>

namespace clocksource
{


struct Internal
{

static AbsTime
getCurrentReferenceTicks();

static AbsTime
getTimeSinceReferenceStable_us();

};


template <unsigned inputPinNr, AbsTime referenceClockFrequency>
struct External
{
    PIO pio;
    uint sm;
    uint offset;
    int dmaChannel;

    // This is static, so for the same pin we can't have different counters callbacks.
    // ... which seems sensible
    static uint32_t counterLow;
    static uint32_t counterHigh;

    static void
    counterHasWrapped()
    {
        counterHigh++;
    }

public:
    External()
    {
        counterLow = 0;
        counterHigh = 0;
        gpio_init(inputPinNr);
        gpio_set_pulls(inputPinNr, false, true);    // "Weak" pulldown

        if (!pio_claim_free_sm_and_add_program(&pulsecounter_program, &pio, &sm, &offset))
        {
            printf("Err: could not claim free PIO sm for pulsecounter program\n");
        }

        pulsecounter_program_init(pio, sm, offset, inputPinNr);

        dmaChannel = dma_claim_unused_channel(true);
        if (dmaChannel < 0)
        {
            printf("Err: Could not claim unused DMA channel\n");
        }

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

        irq_add_shared_handler(pio_irq, &External::counterHasWrapped, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY); // Add a shared IRQ handler
        irq_set_enabled(pio_irq, true); // Enable the IRQ
        const uint irq_index = pio_irq - pio_get_irq_num(pio, 0); // Get index of the IRQ
        pio_set_irqn_source_enabled(pio, irq_index, pio_interrupt_source::pis_interrupt0, true);

        printf("External source inited.\n");
    }

    std::optional<uint32_t>
    lookIntoStateMachine()
    {
        printf("Fifo level TX: %u, RX: %u\n",
                pio_sm_get_tx_fifo_level(pio, sm),
                pio_sm_get_rx_fifo_level(pio, sm));
        printf("Is at instr. %u\n", pio_sm_get_pc(pio, sm) - offset);
        if (pio_sm_is_rx_fifo_empty(pio, sm))
        {
            return std::nullopt;
        }
        return pio_sm_get(pio, sm);
    }

    AbsTime
    getCurrentReferenceTicks()
    {
        return counterLow | static_cast<AbsTime>(counterHigh) << 32;
    }

    AbsTime
    getTimeSinceReferenceStable_us()
    {
        static constexpr AbsTime multiplicator = referenceClockFrequency / 1'000'000;
        static_assert (multiplicator * 1'000'000 == referenceClockFrequency,
                        "ReferenceClock not divisible by microseconds without loss");
        return getCurrentReferenceTicks() / multiplicator;
    }
};

// I hope that this does not lead to multiple different counters...

template <unsigned inputPinNr, AbsTime referenceClockFrequency>
uint32_t External<inputPinNr, referenceClockFrequency>::counterLow = 0;

template <unsigned inputPinNr, AbsTime referenceClockFrequency>
uint32_t External<inputPinNr, referenceClockFrequency>::counterHigh = 0;


} // namespace clocksource