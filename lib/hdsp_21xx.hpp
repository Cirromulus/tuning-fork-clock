// from https://github.com/koendv/retroleds/blob/master/arduino/retroleds/retroleds.ino

/*

   #           #                           #####       #
   #           #                          #     #     ##
   ####    #####   ####   #####                 #      #  #    #  #    #
   #   #  #    #  #        #   #   #####    ####       #   #  #    #  #
   #   #  #    #   ####    #   #           #           #    ##      ##
   #   #  #    #       #   ####           #            #   #  #    #  #
  ##   #   #### # #####    #              #######      #  #    #  #    #
                          ##

  16x2 character display

*/

/*
  Copyright (c) 2019 Koen De Vleeschauwer.  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  - Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  - Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <mcp23017.h>
#include <array>
#include <optional>
#include <string_view>

struct HDSP21XXPins
{
  using Pin = uint8_t;

  std::array<Pin, 8> data;
  std::array<Pin, 5> address;
  Pin chipEnable;
  std::optional<Pin> read;  // not connected in my setup
  Pin write;
  Pin flash;
  std::optional<Pin> reset; // not connected in my setup
};

// this is a workaround for a compiler-bug (?)
//https://stackoverflow.com/questions/53408962/try-to-understand-compiler-error-message-default-member-initializer-required-be
namespace hdsp21xx
{
  struct StringOptions
  {
    enum class Alignment
    {
      left,
      right
      // center // not needed, so not implemented
    };

    bool blink = false;
    Alignment alignment = Alignment::left;
    bool nofill = false;  // do not fill unset chars with spaces
  };

  struct RunningTextOptions
  {
    uint32_t per_char_wait_us = 100'000;
    uint32_t initial_wait_us = 700'000;
    uint32_t end_wait_us = initial_wait_us / 2;
    bool blink = false;
    bool clear_screen = false;
    uint32_t cycles = 1;
    bool fade_in = false;   // if active, initial_wait_us is ignored
    bool fade_out = false;  // if active, end_wait_us is ignored
  };
}

// This was downgraded to only work for one single display.
// template <HDSP21XXPins pinSetup> // sadly, this was over my compiler's abilities because of std::optional
class HDSP21XX
{
  // we will never delay so much as a second
  using Microseconds = uint32_t;
  static constexpr Microseconds clearScreenHoldoff = 110 + 100; // extra, for safety! :D
  static constexpr Microseconds dataApply = 1;  // I did not find a value in the datasheet

public:
  using StringOptions = hdsp21xx::StringOptions;
  using RunningTextOptions = hdsp21xx::RunningTextOptions;
  using Brightness = uint8_t;
  static constexpr Brightness maxBrightness = 7;
  static constexpr size_t num_characters = 8;

  constexpr HDSP21XX(const HDSP21XXPins& pinSetup, Mcp23017& expander)
  : mPinSetup{pinSetup}, mcp{expander}
  {
    mcp.set_io_direction(0);
    mcp.set_all_output_bits(0);

    clear_screen();
    set_brightness(5);  // Set default brightness and allow character blinking
  }

  void reset()
  {
    if (mPinSetup.reset.has_value())
    {
        /* Pull HDSP-21xx reset pin low */
        mcp.set_output_bit_for_pin(*mPinSetup.reset, false);
        mcp.flush_output();
        sleep_ms(1); /* Needs three clock cycles for hardware reset */
        mcp.set_output_bit_for_pin(*mPinSetup.reset, true);
        mcp.flush_output();
        sleep_ms(1);
    }
  }

  /* blink character at position pos */
  void blink_char(uint8_t pos, bool blink) {
    uint8_t col = pos & 0x7;

    uint8_t addr = col;
    uint8_t dta =  blink ? 0x1 : 0x0;

    write_cycle(addr, dta, true); // This is the only write_cycle where flash is true.

    return;
  }

  // write a character of the built-in character set on postion pos
  // the character is ascii or katakana, depending on the built-in character set of the hdsp-21xx.
  void write_builtin_char(uint8_t pos, uint8_t ch, bool blinking = false)
  {
      uint8_t col = pos & 0x7;

      uint8_t addr = col | 0x18;
      uint8_t dta = ch & 0x7f;

      write_cycle(addr, dta);
      blink_char(pos, blinking);
  }

  void
  write_string_oneshot(const std::string_view& str, const StringOptions& options = {})
  {
    const std::string_view sanitizedStr = str.substr(0, num_characters);
    const size_t startOfString = options.alignment == StringOptions::Alignment::left ? 0 : num_characters - sanitizedStr.size();

    for (size_t i = 0; !options.nofill && i < startOfString; i++)
    {
      write_builtin_char(i, ' ');
    }
    for (size_t i = startOfString; i < startOfString + sanitizedStr.size(); i++)
    {
      write_builtin_char(i, sanitizedStr[i - startOfString], options.blink);
    }
    for (size_t i = startOfString + sanitizedStr.size(); !options.nofill && i < num_characters; i++)
    {
      write_builtin_char(i, ' ');
    }
  }

  /**
   * Blocking!!!!1!1!
   */
  void
  write_string_running(const std::string_view& str, const RunningTextOptions& options = {})
  {
    // todo: Add possibility to not override blink in write_string_oneshot so that we can blink the complete text here
    for (size_t cycle = 0; cycle < options.cycles; cycle++)
    {
      if (options.fade_in)
      {
        size_t pointer = 0;
        // coming in
        for (; pointer < str.size(); pointer++)
        {
          write_string_oneshot(str.substr(0, pointer), {.alignment = StringOptions::Alignment::right});  // blink is not good during move
          sleep_us(options.per_char_wait_us);
        }
        // if str is smaller than display, fade to leftalignment
        for (;pointer <= num_characters; pointer++)
        {
          const size_t leftpad = num_characters - pointer;
          // uff, transition from right alignment to left if string smaller than num_chars
          // const bool leftpad = pointer - str.size();
          for (size_t i = 0; i < num_characters; i++)
          {
            if (i < leftpad)
              write_builtin_char(i, ' '); // starting
            else if (i < leftpad + str.size())
              write_builtin_char(i, str[i - leftpad]); // actual text
            else
              write_builtin_char(i, ' '); // trailing
          }
          sleep_us(options.per_char_wait_us);
        }
      }
      else
      {
        write_string_oneshot(str, {.blink = options.blink});
        if (str.size() <= num_characters)
        {
          // why even bother?
          return;
        }
        sleep_us(options.initial_wait_us);
      }

      size_t pointer = 1;
      for (; pointer + num_characters < str.size(); pointer++)
      {
        write_string_oneshot(str.substr(pointer, num_characters));
        sleep_us(options.per_char_wait_us);
      }

      if (options.fade_out)
      {
        for (; pointer <= str.size(); pointer++)
        {
          write_string_oneshot(str.substr(pointer, str.size() - pointer),
                               {.alignment = StringOptions::Alignment::left});
          sleep_us(options.per_char_wait_us);
        }
      }
      else
      {
        if (options.blink)
        {
          for (size_t i = 0; i < num_characters; i++)
          {
            blink_char(i, true);
          }
        }

        sleep_us(options.end_wait_us);

        if (options.clear_screen)
        {
          clear_screen();
        }
      }
    }
  }

  // set display brightness 0..7. Also enables character blinking.
  void set_brightness(uint8_t i)
  {
    mCurrentBrightness = maxBrightness - i;
    write_cycle(0x10, mCurrentBrightness & 0x7 | 0x08); /* Brightness, Figure 6 in datasheet */
  }
    // erase internal character buffer and clear screen
  void clear_screen() {
    sleep_us(1);  // tjoa
    write_cycle(0x10, 0x80 | mCurrentBrightness & 0x7 | 0x08); /* Clear display, Figure 6 in datasheet */
    sleep_us(clearScreenHoldoff);
    return;
  }

private:
  // low-level routines

  /* set hdsp-21xx address and data bus values on bus extender */
  void set_addr_dta(uint8_t addr, uint8_t dta)
  {
    // address
    for (size_t i = 0; i < mPinSetup.address.size(); i++)
    {
      mcp.set_output_bit_for_pin(mPinSetup.address[i], addr & (1 << i));
    }
    // data
    for (size_t i = 0; i < mPinSetup.data.size(); i++)
    {
      mcp.set_output_bit_for_pin(mPinSetup.data[i], dta & (1 << i));
    }
    mcp.flush_output();
  }

  // write one byte on hdsp bus
  // disp: display number, 0..3
  // addr: address
  // dta: data
  // flash: if true, access "flash" memory to store blinking attribute.
  void write_cycle(uint8_t addr, uint8_t dta, bool flash = false)
  {
    // we expect chipEnable to be active low,
    // and to be always set to true afterwards by our functions
    mcp.set_output_bit_for_pin(mPinSetup.flash, !flash);
    mcp.set_output_bit_for_pin(mPinSetup.write, false);
    set_addr_dta(addr, dta);  // this also flushes
    mcp.set_output_bit_for_pin(mPinSetup.chipEnable, false);
    mcp.flush_output();
    sleep_us(dataApply);
    mcp.set_output_bit_for_pin(mPinSetup.write, true);
    mcp.set_output_bit_for_pin(mPinSetup.chipEnable, true);
    mcp.set_output_bit_for_pin(mPinSetup.flash, true);
    mcp.flush_output();
  }

  // print blinking question mark as error indicator
  void error() {
    write_builtin_char(0, '?', true);
    return;
  }


  HDSP21XXPins mPinSetup;
  Mcp23017& mcp;
  Brightness mCurrentBrightness = 5;  // up to 7! Wow!
};
