#pragma once

// Native unit tests only: just enough of ESP-IDF's driver/gpio.h for
// include/config.h to compile on a PC.
typedef enum { GPIO_NUM_0 = 0 } gpio_num_t;
