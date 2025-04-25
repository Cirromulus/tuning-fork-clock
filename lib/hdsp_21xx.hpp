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
#include <optional>  // Lol this will never be prod enough

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

// This was downgraded to only work for one single display.
// template <HDSP21XXPins pinSetup> // sadly, this was over my compiler's abilities because of std::optional
class HDSP21XX
{
  // Display buffer
  struct display_char
  {
    uint8_t ch;
    // uint8_t font_id; // not supported any more, currently
    bool blinking;
  };
  using Brightness = uint8_t;

public:
  static constexpr size_t num_characters = 8;

  constexpr HDSP21XX(const HDSP21XXPins& pinSetup, Mcp23017& expander)
  : mPinSetup{pinSetup}, mcp{expander}
  {
    mcp.set_io_direction(0);
    mcp.set_all_output_bits(0);

    clear_screen();
    set_brightness(6);  // Set default brightness and allow character blinking
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

  // set display brightness 0..7. Also enables character blinking.
  void set_brightness(uint8_t i)
  {
    mCurrentBrightness = i;
    write_cycle(0x10, mCurrentBrightness & 0x7 | 0x08); /* Brightness, Figure 6 in datasheet */
  }

  // write internal character buffer to display
  void update() {
    for (size_t i = 0; i < mDisplay.size(); i++)
    {
      write_builtin_char(i, mDisplay[i].ch, mDisplay[i].blinking);
    }
  }

    // erase internal character buffer and clear screen
  void clear_screen() {
    for (auto& c : mDisplay)
    {
      c.ch = ' ';
      c.blinking = false;
    }
    write_cycle(0x10, 0x80 | mCurrentBrightness & 0x7 | 0x08); /* Clear display, Figure 6 in datasheet */
    sleep_ms(1); /* Needs three clock cycles (110 us) to execute, so 1 ms is more than enough */
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
    mcp.set_output_bit_for_pin(mPinSetup.flash, !flash);
    set_addr_dta(addr, dta);
    sleep_ms(1);
    mcp.set_output_bit_for_pin(mPinSetup.chipEnable, false);
    mcp.flush_output();
    sleep_ms(1);
    mcp.set_output_bit_for_pin(mPinSetup.write, false);
    mcp.flush_output();
    sleep_ms(1);
    mcp.set_output_bit_for_pin(mPinSetup.write, true);
    sleep_ms(1);
    mcp.set_output_bit_for_pin(mPinSetup.chipEnable, true);
    mcp.set_output_bit_for_pin(mPinSetup.flash, true);
    mcp.flush_output();
    sleep_ms(1);
  }

  // print blinking question mark as error indicator
  void error() {
    write_builtin_char(0, '?', true);
    return;
  }


  HDSP21XXPins mPinSetup;
  Mcp23017& mcp;
  std::array<display_char, num_characters> mDisplay;
  Brightness mCurrentBrightness = 5;  // up to 7! Wow!
};
