#include "config.hpp"

#include <pico/stdlib.h>
#include <pico/multicore.h>

#include <stdio.h>
#include <array>
#include <optional>
#include <iostream>


int main() {
    setup_default_uart();
    stdio_init_all();

    while(true)
    {
        std::cout << "Dei' Mudda schwitzt beim Kaggne" << std::endl;
        sleep_ms(500);
    }

    return 0;
}
