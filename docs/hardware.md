# Hardware

The GPIO assignments below match `src/config.h` and peripheral initialization in `src/main.cpp`, with MQ-2 on GPIO 34 and DHT11 on GPIO 4.

## Components

| Component | Current firmware configuration |
| --- | --- |
| ESP32 | ESP32-WROOM development board, PlatformIO `esp32dev` environment |
| Temperature/humidity sensor | DHT11, DATA on GPIO 4 |
| Gas sensor | MQ-2 analog output on GPIO 34 |
| OLED | SH1106, 128 × 64, four-wire SPI |

## OLED connections

| OLED signal | ESP32 connection |
| --- | --- |
| GND | GND |
| VCC | 3.3 V, as specified in the source wiring comment |
| CLK / SCK | GPIO 18 |
| DIN / MOSI | GPIO 23 |
| CS | GPIO 5 |
| D/C | GPIO 16 |
| RES / RST | GPIO 17 |

This setup is built for SPI OLEDs, not I2C OLEDs. Make sure your OLED uses SPI. If you use an I2C OLED, you must modify the wiring and code.

The code uses the `Adafruit_SH1106G` driver. An SPI SSD1306 display needs the appropriate driver change even if its signal pins are similar.

## Board compatibility

The prototype's module is ESP32-WROOM. `platformio.ini` selects the generic ESP32 Dev Module target, `esp32dev`. GPIO 16 and 17 are available on this WROOM board for the OLED D/C and reset signals, so the existing OLED pin assignments are retained. See [Espressif's pin documentation](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html) and the [PlatformIO board profile](https://docs.platformio.org/en/latest/boards/espressif32/esp32dev.html).

## Sensor signal connections

| Sensor signal | ESP32 connection | Notes |
| --- | --- | --- |
| DHT11 DATA | GPIO 4 | DATA and its pull-up must use ESP32-compatible logic levels; exact breakout supply/pull-up details are not recorded |
| MQ-2 AOUT | GPIO 34 | ADC1 input; analog voltage must be limited to the ESP32 input range |
| MQ-2 DOUT | Not connected | Current firmware uses analog input only |
| Sensor grounds | Common GND | Shared reference with the ESP32 and OLED |

GPIO assignments do not specify sensor power. The MQ-2 manufacturer's standard test circuit uses a 5 V heater and a 5 V sensing circuit; its analog output can exceed the ESP32's input range. A module without built-in output scaling needs an appropriate divider or conditioning circuit before GPIO 34. The original MQ-2 supply and conditioning circuit are not recorded. See [Winsen's MQ-2 specifications](https://www.winsen-sensor.com/sensors/combustible-sensor/mq2.html).

## Gas-estimation assumptions

Voltage scaling means converting the voltage the ESP32 reads back into the sensor's actual output voltage. For example, a divider that halves a 4 V sensor output makes the ESP32 read 2 V; the calculation needs to account for that reduction.

`src/mq2.cpp` currently converts raw ADC counts using `raw / 4095 * 3.3` and calculates sensor resistance as `(3.3 - v) / v * RL`. `src/config.h` sets `RL` to 10 kOhm. This assumes a 3.3 V sensing circuit, a matching load resistor, and no unaccounted output divider. It does not model a 5 V sensing circuit or a separate divider. Those circuit details determine whether the calculation applies.

The raw-count voltage conversion is also an approximation: `analogRead()` returns an uncalibrated value, while Espressif provides `analogReadMilliVolts()` for calibrated voltage readings. See the [Arduino-ESP32 ADC documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html).

Until the sensor circuit and conversion are matched, treat displayed gas concentrations as prototype estimates. The pin map alone does not establish concentration accuracy.

The firmware begins with a configured 20-second warm-up and attempts a clean-air baseline calibration. This is the software sequence, not evidence that the sensor has completed the conditioning required for accurate measurements. Calibration failure reports gas as unavailable rather than using a default baseline to publish an estimate; see serial diagnostics. Saturated or unusable ADC input is also unavailable, and an existing alarm remains active until a valid below-threshold measurement.
