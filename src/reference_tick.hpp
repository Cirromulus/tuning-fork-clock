#pragma once

#include <include/config.hpp>

#include <stdio.h>      // Only for debug prints
#include <hardware/pio.h>
#include <hardware/dma.h>
// #include <hardware/clocks.h>
#include <pulsecounter.pio.h>
#include <pico/time.h>  // Only used for timeout

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
    static uint32_t counterHigh;

    static void
    counterHasWrapped()
    {
        printf("CounterWrapped\n");
        counterHigh++;
    }

public:
    External()
    {
        counterHigh = 0;
        gpio_init(inputPinNr);
        gpio_set_pulls(inputPinNr, false, true);    // "Weak" pulldown

        if (!pio_claim_free_sm_and_add_program(&pulsecounter_program, &pio, &sm, &offset))
        {
            printf("Err: could not claim free PIO sm for pulsecounter program\n");
        }

        pulsecounter_program_init(pio, sm, offset, inputPinNr);

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

    std::optional<AbsTime>
    getCurrentReferenceTicks() const
    {
        // If we don't already have something inside...
        if (pio_sm_is_tx_fifo_empty(pio, sm))
        {
            pio_sm_put(pio, sm, 1);     // any non-zero value is considered a request
        }

        // this is the internal, possibly drifting, MCU time.
        // Only used for timeout.
        static constexpr unsigned maxCyclesToWait = 2;
        static constexpr uint32_t maxTimePerCycle_us = maxCyclesToWait * (referenceClockFrequency / 1'000'000);
        const auto whenToTimeout = make_timeout_time_us(maxTimePerCycle_us);

        std::optional<AbsTime> counterLow;
        while(absolute_time_diff_us(get_absolute_time(), whenToTimeout) > 0)
        {
            if (!pio_sm_is_rx_fifo_empty(pio, sm))
            {
                counterLow = pio_sm_get(pio, sm);
                break;
            }
        }
        return counterLow.transform([](const AbsTime& value) {return value | static_cast<AbsTime>(counterHigh) << 32 ;});
    }

    std::optional<AbsTime>
    getTimeSinceReferenceStable_us() const
    {
        static constexpr AbsTime multiplicator = referenceClockFrequency / 1'000'000;
        static_assert (multiplicator * 1'000'000 == referenceClockFrequency,
                        "ReferenceClock not divisible by microseconds without loss");
        return getCurrentReferenceTicks().transform([](const AbsTime& val){ return val / multiplicator; });
    }
};

// I hope that this does not lead to multiple different counters...
template <unsigned inputPinNr, AbsTime referenceClockFrequency>
uint32_t External<inputPinNr, referenceClockFrequency>::counterHigh = 0;


} // namespace clocksource