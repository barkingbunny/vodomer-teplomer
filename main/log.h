#pragma once

// Log na USB serial (UART0, 115200). V release buildu (CONFIG_VT_DEBUG=n) vypnuty.

#include <stdio.h>

#include "sdkconfig.h"

#if CONFIG_VT_DEBUG
#include "esp_rom_serial_output.h"
#define LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
// Pred deep sleep: vyprazdnit stdout i HW FIFO UARTu, jinak se konec logu utne.
#define LOG_FLUSH()                                                 \
    do {                                                            \
        fflush(stdout);                                             \
        esp_rom_output_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);   \
    } while (0)
#else
// Argumenty se nevyhodnoti ani neprelozi do kodu, ale kompilator je "vidi" -
// promenne pouzite jen v logu pak nehlasi jako nepouzite.
#define LOG(fmt, ...)                                \
    do {                                             \
        if (0) printf(fmt "\n", ##__VA_ARGS__);      \
    } while (0)
#define LOG_FLUSH() \
    do {            \
    } while (0)
#endif
