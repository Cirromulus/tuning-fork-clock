#pragma once

#include <hardware/uart.h>

#include <optional>
#include <array>

class Serial
{
    // TODO: This should become an abstraction for stdio (USB) and any uart somewhen
public:
    constexpr
    Serial(uart_inst_t* uart) : mUartHandle{uart}, mPrintBuffer{}
    {
    }

    template<class...Args>
    void
    print(const char* format, Args&&...args)
    {
        const auto maybeString = string(format, std::forward<Args>(args)...);
        uart_puts(mUartHandle, maybeString.value_or("-- string too big error --"));
    }

private:
    template<class...Args>
    std::optional<const char*>
    string(const char* format, Args&&...args)
    {
        const int chars = std::snprintf(mPrintBuffer.begin(), mPrintBuffer.size(), format, std::forward<Args>(args)...);
        if (chars > 0)
        {
            return mPrintBuffer.begin();
            // return std::string_view{mPrintBuffer.begin(), static_cast<unsigned int>(chars)};
        }
        else
        {
            return std::nullopt;
        }
    }

    uart_inst_t* mUartHandle;
    std::array<char, 128> mPrintBuffer;
};