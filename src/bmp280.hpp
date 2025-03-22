#pragma once

#include "bmp280_detail.hpp"

#include <hardware/i2c.h>

#include <array>
#include <optional>

using namespace copied_from_adafruit;

class BMP280
{
    using RegAddr = uint8_t;
    static constexpr uint8_t deviceAddr = 0x76;
    static constexpr size_t timeout_ms = 100;

public:
    constexpr BMP280(i2c_inst_t* i2c)
    : m_i2c{i2c}
    {
        {
            ctrl_meas ugly_measurement_reg;
            ugly_measurement_reg.mode = MODE_NORMAL;
            ugly_measurement_reg.osrs_t = SAMPLING_X16;
            ugly_measurement_reg.osrs_p = SAMPLING_X16;

            writeReg8(BMP280_REGISTER_CONFIG, ugly_measurement_reg.get());
        }

        {
            config ugly_config_reg;
            ugly_config_reg.filter = FILTER_OFF;
            ugly_config_reg.t_sb = STANDBY_MS_250;

            writeReg8(BMP280_REGISTER_CONTROL, ugly_config_reg.get());
        }
    }

    std::optional<uint32_t>
    readTemperatureRaw()
    {
        return readReg<3>(BMP280_REGISTER_TEMPDATA).transform(combineOrder<3>);
    }

    std::optional<uint32_t>
    readPressureRaw()
    {
        return readReg<3>(BMP280_REGISTER_PRESSUREDATA).transform(combineOrder<3>);
    }

private:
    template <size_t width>
    static constexpr uint32_t
    combineOrder(const std::array<uint8_t, width>& regs)
    {
        static_assert(width >= 1);
        // actual byte order IDK lol
        uint32_t comb = 0;
        for (size_t i = 0; i < regs.size(); i++)
        {
            comb |= regs[i] << ((regs.size() - (i+1)) * 8);
        }
        return comb;
    }

    template <size_t width = 1>
    std::optional<std::array<uint8_t, width>>
    readReg(RegAddr addr)
    {
        // write then read.
        /*
        To be able to read registers, first the register address must be sent in write mode
        (slave address 111011X0).
        Then either a stop or a repeated start condition must be generated.
        After this the slave is addressed in read mode (RW = ‘1’) at address 111011X1,
        after which the slave sends out data from auto-incremented register addresses
        until a NOACKM and stop condition occurs.
        */

        if (i2c_write_blocking_until(m_i2c, deviceAddr, &addr, 1, true, make_timeout_time_ms(timeout_ms)) != 1)
        {
            return std::nullopt;
        }

        std::array<uint8_t, width> buffer = {};

        if (i2c_read_blocking_until(m_i2c, deviceAddr, buffer.begin(), buffer.size(), false, make_timeout_time_ms(timeout_ms)) != width)
        {
            return std::nullopt;
        }

        return buffer;
    }

    bool
    writeReg8(RegAddr addr, uint8_t val)
    {
        std::array<uint8_t, 2> raw {addr, val};
        return i2c_write_blocking_until(m_i2c, deviceAddr, raw.begin(), raw.size(), false, make_timeout_time_ms(timeout_ms)) == raw.size();
    }

    i2c_inst_t* m_i2c;
};