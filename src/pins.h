#pragma once

// Every GPIO the firmware uses. See README.md for the wiring.

// I2S output of each audio unit. The PIO-based I2S driver requires LRC to be
// the pin right after BCLK.
struct I2sPins {
  int bclk;
  int lrclk;
  int din;
};
const int MAX_AUDIO_UNITS = 4;
const I2sPins AUDIO_UNIT_PINS[MAX_AUDIO_UNITS] = {
    {0, 1, 2},
    {4, 5, 6},
    {8, 9, 10},
    {12, 13, 14},
};

// SD card reader; must be pins of SPI0 (the SPI object).
const int SD_PIN_MISO = 16;
const int SD_PIN_CS = 17;
const int SD_PIN_SCK = 18;
const int SD_PIN_MOSI = 19;

// Status display; must be pins of I2C0 (the Wire object).
const int DISPLAY_PIN_SDA = 20;
const int DISPLAY_PIN_SCL = 21;
