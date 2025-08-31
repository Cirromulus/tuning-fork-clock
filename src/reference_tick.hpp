#pragma once

#include <include/config.hpp>

#include <stdio.h>      // Only for debug prints
#include <hardware/pio.h>
#include <hardware/dma.h>
// #include <hardware/clocks.h>
#include <pulsecounter.pio.h>
#include <pico/time.h>  // Only used for timeout

#include <atomic>
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
    // This is static, so for the same pin we can't have different counters callbacks.
    // ... which seems sensible

    static inline PIO pio;
    static inline uint sm;
    static inline uint offset;
    static constexpr uint counterWrapPioItr = 0;
    static inline std::atomic<uint32_t> counterHigh;

    using PioRegisterWidth = uint32_t;

    static void
    counterHasWrapped()
    {
        pio_interrupt_clear(pio, counterWrapPioItr) ;
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

        // Find a free irq
        int pio_irq = pio_get_irq_num(pio, counterWrapPioItr);
        if (irq_get_exclusive_handler(pio_irq)) {
            pio_irq++;
            if (irq_get_exclusive_handler(pio_irq)) {
                panic("All IRQs are in use");
            }
        }

        // TODO: If we want to have multiple IRQ handlers for our PIO,
        // This would need to be changed to .._shared_.. and the ITR handler needs to check the source.
        irq_set_exclusive_handler(pio_irq, &External::counterHasWrapped);
        irq_set_enabled(pio_irq, true); // Enable the IRQ
        const uint irq_index = pio_irq - pio_get_irq_num(pio, 0); // Get index of the IRQ
        pio_set_irqn_source_enabled(pio, irq_index, pio_interrupt_source::pis_interrupt0, true);

        pulsecounter_program_init(pio, sm, offset, inputPinNr);

        printf("External source inited.\n");
    }

    std::optional<uint32_t>
    lookIntoStateMachine() const
    {
        printf("\tCounterHigh is %lu\n", counterHigh.load());
        printf("\tFifo level TX: %u, RX: %u\n",
                pio_sm_get_tx_fifo_level(pio, sm),
                pio_sm_get_rx_fifo_level(pio, sm));
        printf("\tIs at instr. %u\n", pio_sm_get_pc(pio, sm) - offset);
        if (pio_sm_is_rx_fifo_empty(pio, sm))
        {
            printf("\tFIFO empty\n");
            return std::nullopt;
        }
        return pio_sm_get(pio, sm);
    }

    std::optional<AbsTime>
    getCurrentReferenceTicks() const
    {
        // flush rx fifo in case there was a timeout
        while (!pio_sm_is_rx_fifo_empty(pio, sm))
        {
            // discard possibly old values
            pio_sm_get(pio, sm);
        }
        // If we don't already have something inside...
        // This may happen if we time-outed from last read (and still will timeout)
        if (pio_sm_is_tx_fifo_empty(pio, sm))
        {
            pio_sm_put(pio, sm, 1);     // any non-zero value is considered a request
        }

        // We have two response slots per cycle
        static constexpr unsigned maxCyclesToWait = 3;
        static constexpr AbsTime maxTimePerCycle_us = maxCyclesToWait * (referenceClockFrequency / 1'000'000);
        // this is the internal, possibly drifting, MCU time. Only used for timeout.
        const auto whenToTimeout = make_timeout_time_us(maxTimePerCycle_us);

        std::optional<PioRegisterWidth> counterLow;
        do  // at least once.
        {
            if (!pio_sm_is_rx_fifo_empty(pio, sm))
            {
                counterLow = pio_sm_get(pio, sm);
                break;
            }
        }
        while(absolute_time_diff_us(get_absolute_time(), whenToTimeout) > 0);

        if (counterLow.has_value())
        {
            return *counterLow | (static_cast<AbsTime>(counterHigh) << (sizeof(PioRegisterWidth) * 8));
        }

        // printf("External Tick Timeout:\n");
        // lookIntoStateMachine();
        return std::nullopt;
    }

    std::optional<AbsTime>
    getTimeSinceReferenceStable_us() const
    {
        static constexpr AbsTime us_per_s = 1'000'000;
        static constexpr AbsTime frequency_Mhz = referenceClockFrequency / us_per_s;
        static constexpr AbsTime us_per_tick = us_per_s / referenceClockFrequency;
        // Frequency is >= 1 Mhz, and we can divide without loss.
        static constexpr bool canDivideWithoutLoss = frequency_Mhz * us_per_s == referenceClockFrequency;
        // Frequency is < 1Mhz, and we can multiply without loss.
        static constexpr bool canMultiplyWithoutLoss = us_per_tick * referenceClockFrequency == us_per_s;

        const auto now_ticks = getCurrentReferenceTicks();
        if (!now_ticks)
        {
            return std::nullopt;
        }
        if constexpr (canDivideWithoutLoss)
        {
            return *now_ticks / frequency_Mhz;
        }
        else if constexpr (canMultiplyWithoutLoss)
        {
            return *now_ticks * us_per_tick;
        }
        else
        {
            // comment out and return a double, if you have a problem with that
            static_assert (canDivideWithoutLoss || canMultiplyWithoutLoss,
                            "ReferenceClock not divisible by microseconds without loss");
        }

    }
};

} // namespace clocksource
