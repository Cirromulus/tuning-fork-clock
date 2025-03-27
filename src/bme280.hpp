#pragma once

#include "bme280_detail.hpp"

#include <hardware/i2c.h>

#include <array>
#include <optional>


// FIXME DEBUg
#include <cstdio>


using namespace copied_from_adafruit;

class BME280
{
    using RegAddr = uint8_t;
    static constexpr uint8_t deviceAddr = 0x76;
    static constexpr uint8_t deviceChipId = 0x60;
    static constexpr size_t timeout_ms = 100;

public:
    constexpr BME280(i2c_inst_t* i2c)
    : m_i2c{i2c}
    {
    }

    bool init()
    {
        const auto chipId = readReg<uint8_t>(BME280_REGISTER_CHIPID);
        if (!chipId || *chipId != deviceChipId)
        {
            return false;
        }

          // if chip is still reading calibration, delay
        while (isReadingCalibration())
            sleep_ms(10);

        readCoefficients();

        return startSampling();
    }

    std::optional<uint32_t>
    readTemperatureRaw()
    {
        return readRegThree(BME280_REGISTER_TEMPDATA);
    }

    std::optional<uint32_t>
    readPressureRaw()
    {
        return readRegThree(BME280_REGISTER_PRESSUREDATA);
    }

    std::optional<uint16_t>
    readHumidityRaw()
    {
        return readReg<uint16_t>(BME280_REGISTER_HUMIDDATA);
    }

    // TODO: Is hugely incorrect currently
    // Returns temperature in DegC, resolution is 0.01 DegC.
    // Output value of "5123" equals 51.23 DegC.
    std::optional<int32_t>
    readTemperature()
    {
        const auto maybeRawTemp = readTemperatureRaw();
        if (!maybeRawTemp || *maybeRawTemp == 0x800000) // value in case temp measurement was disabled
        {
            return std::nullopt;
        }

        const int32_t adc_T = *maybeRawTemp;  // notice conversion to signed
        printf("Read raw: %d\n", adc_T);
        printf("T1: %d, T2: %d, T3: %d\n",
            (int32_t) m_calibration.dig_T1,
            (int32_t) m_calibration.dig_T2,
            (int32_t) m_calibration.dig_T3);

        // // Following uglyness is from bosch's ugly calib code
        int32_t var1, var2;
        var1 = ((((adc_T>>3) - ((int32_t) m_calibration.dig_T1<<1))) *
                ((int32_t) m_calibration.dig_T2)) >> 11;
        var2 = (((((adc_T>>4) - ((int32_t)m_calibration.dig_T1)) *
                ((adc_T>>4) - ((int32_t) m_calibration.dig_T1))) >> 12) *
                ((int32_t)m_calibration.dig_T3)) >> 14;

        return ((var1 + var2) * 5 + 128) >> 8;

        // Adafruits version
        // const int32_t adc_T = *maybeRawTemp >> 4;
        // int32_t var1, var2;
        // var1 = (int32_t)((adc_T / 8) - ((int32_t)m_calibration.dig_T1 * 2));
        // var1 = (var1 * ((int32_t)m_calibration.dig_T2)) / 2048;
        // var2 = (int32_t)((adc_T / 16) - ((int32_t)m_calibration.dig_T1));
        // var2 = (((var2 * var2) / 4096) * ((int32_t)m_calibration.dig_T3)) / 16384;
        // printf("var1: %d, var2: %d\n", var1, var2);

        // return ((var1 + var2) * 5 + 128) / 256;
    }

private:
    bool
    startSampling(/* TODO: Parameters*/)
    {
        // making sure sensor is in sleep mode before setting configuration
        // as it otherwise may be ignored
        write8(BME280_REGISTER_CONTROL, MODE_SLEEP);


        {
            ctrl_hum hum;
            hum.osrs_h = SAMPLING_X16;
            write8(BME280_REGISTER_CONTROLHUMID, hum.get());
        }

        {
            config conf;
            conf.filter = FILTER_X2;
            conf.t_sb = STANDBY_MS_1000;
            conf.spi3w_en = 0;
            write8(BME280_REGISTER_CONFIG, conf.get());
        }

        {
            ctrl_meas meas;
            meas.mode = MODE_NORMAL;
            meas.osrs_t = SAMPLING_X16;
            meas.osrs_p = SAMPLING_X16;
            // you must make sure to also set REGISTER_CONTROL after setting the
            // CONTROLHUMID register, otherwise the values won't be applied
            // (see DS 5.4.3)
            write8(BME280_REGISTER_CONTROL, meas.get());
        }

        return true;
    }

    /*!
    *   @brief  Reads the factory-set coefficients
    */
    void readCoefficients()
    {
        m_calibration.dig_T1 = readReg<uint16_t>(BME280_REGISTER_DIG_T1).value_or(-1);
        m_calibration.dig_T2 = readReg<int16_t>(BME280_REGISTER_DIG_T2).value_or(-1);
        m_calibration.dig_T3 = readReg<int16_t>(BME280_REGISTER_DIG_T3).value_or(-1);

        m_calibration.dig_P1 = readReg<uint16_t>(BME280_REGISTER_DIG_P1).value_or(-1);
        m_calibration.dig_P2 = readReg<int16_t>(BME280_REGISTER_DIG_P2).value_or(-1);
        m_calibration.dig_P3 = readReg<int16_t>(BME280_REGISTER_DIG_P3).value_or(-1);
        m_calibration.dig_P4 = readReg<int16_t>(BME280_REGISTER_DIG_P4).value_or(-1);
        m_calibration.dig_P5 = readReg<int16_t>(BME280_REGISTER_DIG_P5).value_or(-1);
        m_calibration.dig_P6 = readReg<int16_t>(BME280_REGISTER_DIG_P6).value_or(-1);
        m_calibration.dig_P7 = readReg<int16_t>(BME280_REGISTER_DIG_P7).value_or(-1);
        m_calibration.dig_P8 = readReg<int16_t>(BME280_REGISTER_DIG_P8).value_or(-1);
        m_calibration.dig_P9 = readReg<int16_t>(BME280_REGISTER_DIG_P9).value_or(-1);

        m_calibration.dig_H1 = readReg<uint8_t>(BME280_REGISTER_DIG_H1).value_or(-1);
        m_calibration.dig_H2 = readReg<int16_t>(BME280_REGISTER_DIG_H2).value_or(-1);
        m_calibration.dig_H3 = readReg<uint8_t>(BME280_REGISTER_DIG_H3).value_or(-1);

        m_calibration.dig_H4 = ((int8_t)readReg<uint8_t>(BME280_REGISTER_DIG_H4).value_or(-1) << 4) |
                            (readReg<uint8_t>(BME280_REGISTER_DIG_H4 + 1).value_or(-1) & 0xF);
        m_calibration.dig_H5 = ((int8_t)readReg<uint8_t>(BME280_REGISTER_DIG_H5 + 1).value_or(-1) << 4) |
                            (readReg<uint8_t>(BME280_REGISTER_DIG_H5).value_or(-1) >> 4);
        m_calibration.dig_H6 = readReg<int8_t>(BME280_REGISTER_DIG_H6).value_or(-1);
    }

    template <typename T, size_t width = sizeof(T)>
    static constexpr T
    combineOrder(const std::array<uint8_t, width>& regs)
    {
        T comb = 0;
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

    template <typename T>
    std::optional<T>
    readReg(RegAddr addr)
    {
        return readReg<sizeof(T)>(addr).transform(combineOrder<T>);
    }

    // special case
    std::optional<uint32_t>
    readRegThree(RegAddr addr)
    {
        return readReg<3>(addr).transform(combineOrder<uint32_t, 3>);
    }

    bool
    write8(RegAddr addr, uint8_t val)
    {
        std::array<uint8_t, 2> raw {addr, val};
        return i2c_write_blocking_until(m_i2c, deviceAddr, raw.begin(), raw.size(), false, make_timeout_time_ms(timeout_ms)) == raw.size();
    }

    bool
    isReadingCalibration()
    {
        const auto rStatus = readReg<uint8_t>(BME280_REGISTER_STATUS).value_or(1);
        return (rStatus & (1 << 0)) != 0;
    }

    i2c_inst_t* m_i2c;
    bme280_calib_data m_calibration;
};