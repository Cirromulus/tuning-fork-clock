#pragma once

#include "bme280_detail.hpp"

#include <hardware/i2c.h>

#include <array>
#include <cmath>
#include <expected>
#include <string_view>

// // FIXME DEBUg
// #include <cstdio>


using namespace copied_from_adafruit;

class BME280
{
    using RegAddr = uint8_t;
    static constexpr uint8_t deviceAddr = 0x76;
    static constexpr uint8_t deviceChipId = 0x60;
    static constexpr size_t timeout_ms = 100;

public:
    struct EnvironmentMeasurement
    {
        int32_t temperature_centidegree;    // * .01 Celsius
        uint32_t pressure_q23_8;            // * 2^(-8)
        uint32_t humidity_q22_10;           // * 2^(-10)

        constexpr
        double getTemperatureDegree() const
        {
            return temperature_centidegree * 0.01;
        }

        constexpr
        double getPressurePa() const
        {
            return pressure_q23_8 / static_cast<double>(std::pow(2, 8));
        }

        constexpr
        double getHumidityPercentRH() const
        {
            return humidity_q22_10 / static_cast<double>(std::pow(2, 10));
        }
    };

    static constexpr EnvironmentMeasurement invalidMeasurement{-6666, 0, 0};

    constexpr BME280(i2c_inst_t* i2c)
    : m_i2c{i2c}, m_tempFineCoefficient{0}
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

    std::expected<uint32_t, std::string_view>
    readTemperatureRaw()
    {
        return readRegThree(BME280_REGISTER_TEMPDATA);
    }

    std::expected<uint32_t, std::string_view>
    readPressureRaw()
    {
        return readRegThree(BME280_REGISTER_PRESSUREDATA);
    }

    std::expected<uint16_t, std::string_view>
    readHumidityRaw()
    {
        return readReg<uint16_t>(BME280_REGISTER_HUMIDDATA);
    }

    // Returns temperature in DegC, resolution is 0.01 DegC.
    // E.g.: Output value of "5123" equals 51.23 DegC.
    std::expected<int32_t, std::string_view>
    readTemperature()
    {
        return readTemperatureRaw().and_then(filterDefaultRegisterValue<>).transform([this](const auto& reg){ return calibratedTemperature(reg);});
    }

    // Returns pressure in Pa as unsigned 32 bit integer in Q24.8 format
    // (24 integer bits and 8 fractional bits).
    // Output value of “24674867” represents 24674867/256 = 96386.2 Pa = 963.862 hPa
    std::expected<uint32_t, std::string_view>
    readPressure()
    {
        // you should have read Temperature as well, hm
        return readPressureRaw().and_then(filterDefaultRegisterValue<>).transform([this](const auto& reg){ return calibratedPressure(reg);});
    }

    // Returns humidity in %RH as unsigned 32 bit integer in Q22.10 format
    // (22 integer and 10 fractional bits).
    // Output value of “47445” represents 47445/1024 = 46.333 %RH
    std::expected<uint32_t, std::string_view>
    readHumidity()
    {
        // you should have read Temperature as well, hm
        return readHumidityRaw().and_then(filterDefaultRegisterValue<0x8000>).transform([this](const auto& reg){ return calibratedHumidity(reg);});
    }

    // This is the suggested way of reading it, as the tempco data will be fresh
    std::expected<EnvironmentMeasurement, std::string_view>
    readEnvironment()
    {
        const auto maybeTemp = readTemperature();
        const auto maybePres = readPressure();
        const auto maybeHumi = readHumidity();

        if (maybeTemp && maybePres && maybeHumi)
            return EnvironmentMeasurement{*maybeTemp, *maybePres, *maybeHumi};
        return std::unexpected(!maybeTemp ? maybeTemp.error() : (!maybePres ? maybePres.error() : (!maybeHumi ? maybeHumi.error() : "lol")));
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
            copied_from_adafruit::config conf;
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
        // Funny enough, it seems that configuration registers are inverse endian'ed as the value registers
        m_calibration.dig_T1 = readReg<uint16_t, true>(BME280_REGISTER_DIG_T1).value_or(-1);
        m_calibration.dig_T2 = readReg<int16_t, true>(BME280_REGISTER_DIG_T2).value_or(-1);
        m_calibration.dig_T3 = readReg<int16_t, true>(BME280_REGISTER_DIG_T3).value_or(-1);

        m_calibration.dig_P1 = readReg<uint16_t, true>(BME280_REGISTER_DIG_P1).value_or(-1);
        m_calibration.dig_P2 = readReg<int16_t, true>(BME280_REGISTER_DIG_P2).value_or(-1);
        m_calibration.dig_P3 = readReg<int16_t, true>(BME280_REGISTER_DIG_P3).value_or(-1);
        m_calibration.dig_P4 = readReg<int16_t, true>(BME280_REGISTER_DIG_P4).value_or(-1);
        m_calibration.dig_P5 = readReg<int16_t, true>(BME280_REGISTER_DIG_P5).value_or(-1);
        m_calibration.dig_P6 = readReg<int16_t, true>(BME280_REGISTER_DIG_P6).value_or(-1);
        m_calibration.dig_P7 = readReg<int16_t, true>(BME280_REGISTER_DIG_P7).value_or(-1);
        m_calibration.dig_P8 = readReg<int16_t, true>(BME280_REGISTER_DIG_P8).value_or(-1);
        m_calibration.dig_P9 = readReg<int16_t, true>(BME280_REGISTER_DIG_P9).value_or(-1);

        m_calibration.dig_H1 = readReg<uint8_t, true>(BME280_REGISTER_DIG_H1).value_or(-1);
        m_calibration.dig_H2 = readReg<int16_t, true>(BME280_REGISTER_DIG_H2).value_or(-1);
        m_calibration.dig_H3 = readReg<uint8_t, true>(BME280_REGISTER_DIG_H3).value_or(-1);

        m_calibration.dig_H4 = ((int8_t)readReg<uint8_t>(BME280_REGISTER_DIG_H4).value_or(-1) << 4) |
                            (readReg<uint8_t>(BME280_REGISTER_DIG_H4 + 1).value_or(-1) & 0xF);
        m_calibration.dig_H5 = ((int8_t)readReg<uint8_t>(BME280_REGISTER_DIG_H5 + 1).value_or(-1) << 4) |
                            (readReg<uint8_t>(BME280_REGISTER_DIG_H5).value_or(-1) >> 4);
        m_calibration.dig_H6 = readReg<int8_t>(BME280_REGISTER_DIG_H6).value_or(-1);
    }

    template <uint32_t defaultValue = 0x800000>
    static constexpr
    std::expected<uint32_t, std::string_view>
    filterDefaultRegisterValue(uint32_t registerValue)
    {
        if (registerValue == defaultValue)
        {
            return std::unexpected("got default value");
        }
        return registerValue;
    }

    // temperature in DegC, resolution is 0.01 DegC.
    constexpr
    int32_t
    calibratedTemperature(uint32_t adcValue)
    {
        // {
        //     // Whatever uglyness BOSCH generated, it does not work
        //     printf("BOSCH:\n");
        //     const int32_t adc_T = *maybeRawTemp;  // notice conversion to signed
        //     printf("Read raw: %d\n", adc_T);
        //     printf("T1: %d, T2: %d, T3: %d\n",
        //         (int32_t) m_calibration.dig_T1,
        //         (int32_t) m_calibration.dig_T2,
        //         (int32_t) m_calibration.dig_T3);

        //     // // Following uglyness is from bosch's ugly calib code
        //     int32_t var1, var2;
        //     var1 = ((((adc_T>>3) - ((int32_t) m_calibration.dig_T1<<1))) *
        //             ((int32_t) m_calibration.dig_T2)) >> 11;
        //     var2 = (((((adc_T>>4) - ((int32_t)m_calibration.dig_T1)) *
        //             ((adc_T>>4) - ((int32_t) m_calibration.dig_T1))) >> 12) *
        //             ((int32_t)m_calibration.dig_T3)) >> 14;

        //     const int32_t centiDeg = ((var1 + var2) * 5 + 128) >> 8;
        //     printf("var1: %d, var2: %d, = %ld\n", var1, var2, centiDeg);
        // }

        const int32_t adc_T = adcValue >> 4;
        int32_t var1, var2;
        var1 = (int32_t)((adc_T / 8) - ((int32_t)m_calibration.dig_T1 * 2));
        var1 = (var1 * ((int32_t)m_calibration.dig_T2)) / 2048;
        var2 = (int32_t)((adc_T / 16) - ((int32_t)m_calibration.dig_T1));
        var2 = (((var2 * var2) / 4096) * ((int32_t)m_calibration.dig_T3)) / 16384;

        // this updates the global coefficient... ugly but wontfix I am off the clock
        m_tempFineCoefficient = var1 + var2;

        return (m_tempFineCoefficient * 5 + 128) / 256;
    }

    // Returns pressure in Pa as unsigned 32 bit integer in Q24.8 format
    // -> Fixpoint with 8 fractional bits.
    // Output value of “24674867” represents 24674867/256 = 96386.2 Pa = 963.862 hPa
    constexpr
    uint32_t
    calibratedPressure(uint32_t adcValue)
    {
        int64_t var1, var2, p;
        var1 = ((int64_t)m_tempFineCoefficient) - 128000;
        var2 = var1 * var1 * (int64_t)m_calibration.dig_P6;
        var2 = var2 + ((var1 * (int64_t)m_calibration.dig_P5) << 17);
        var2 = var2 + (((int64_t)m_calibration.dig_P4) << 35);
        var1 = ((var1 * var1 * (int64_t)m_calibration.dig_P3) >> 8) +
               ((var1 * (int64_t)m_calibration.dig_P2) << 12);
        var1 =
            (((((int64_t)1) << 47) + var1)) * ((int64_t)m_calibration.dig_P1) >> 33;

        if (var1 == 0) {
          return 0; // avoid exception caused by division by zero
        }
        p = 1048576 - (adcValue >> 4);
        p = (((p << 31) - var2) * 3125) / var1;
        var1 = (((int64_t)m_calibration.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
        var2 = (((int64_t)m_calibration.dig_P8) * p) >> 19;

        return ((p + var1 + var2) >> 8) + (((int64_t)m_calibration.dig_P7) << 4);
    }

    constexpr
    uint32_t
    calibratedHumidity(uint32_t adcValue)
    {
        // oh no, adafruit left me in the stich
        // need to use ugly and mabye wrong BOSCH calculations
        int32_t v_x1_u32r;
        v_x1_u32r = m_tempFineCoefficient - ((int32_t)76800);
        v_x1_u32r = (((((adcValue << 14) - (((int32_t)m_calibration.dig_H4) << 20) - (((int32_t)m_calibration.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) * (((((((v_x1_u32r * ((int32_t)m_calibration.dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)m_calibration.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * ((int32_t)m_calibration.dig_H2) + 8192) >> 14));
        v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)m_calibration.dig_H1)) >> 4));
        v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
        v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
        return (uint32_t)(v_x1_u32r>>12);
    }

    template <typename T, bool bigendian = false, size_t width = sizeof(T)>
    static constexpr T
    combineOrder(const std::array<uint8_t, width>& regs)
    {
        T comb = 0;
        for (size_t i = 0; i < regs.size(); i++)
        {
            if constexpr (bigendian)
                comb |= regs[i] << (i * 8);
            else
                comb |= regs[i] << ((regs.size() - (i+1)) * 8);
        }
        return comb;
    }

    static constexpr
    std::string_view
    i2cValueToError(int val)
    {
        if (val >= 0)
            return "Not enough bytes received";
        if (val == PICO_ERROR_GENERIC)
            return "address not acknowledged or no device present";
        if (val == PICO_ERROR_TIMEOUT)
            return "a timeout occurred";
        else
            return "Unknown code";
    }

    template <size_t width = 1>
    std::expected<std::array<uint8_t, width>, std::string_view>
    readReg(RegAddr addr)
    {
        // "write then read"

        const int write = i2c_write_blocking_until(m_i2c, deviceAddr, &addr, 1, true, make_timeout_time_ms(timeout_ms));
        if (write != 1)
        {
            return std::unexpected(i2cValueToError(write));
        }

        std::array<uint8_t, width> buffer = {};

        const int read = i2c_read_blocking_until(m_i2c, deviceAddr, buffer.begin(), buffer.size(), false, make_timeout_time_ms(timeout_ms));
        if (read != width)
        {
            return std::unexpected(i2cValueToError(read));
        }

        return buffer;
    }

    template <typename T, bool bigendian = false>
    std::expected<T, std::string_view>
    readReg(RegAddr addr)
    {
        return readReg<sizeof(T)>(addr).transform(combineOrder<T, bigendian>);
    }

    // special case
    template <bool bigendian = false>
    std::expected<uint32_t, std::string_view>
    readRegThree(RegAddr addr)
    {
        return readReg<3>(addr).transform(combineOrder<uint32_t, bigendian, 3>);
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
    int32_t m_tempFineCoefficient;  // updates with every temperature read
};