# Animotion

> A cross-platform animation engine for ESP microcontrollers. Convert GIFs into optimized, firmware-ready assets and C/C++ code with display adapters and a reusable, non-blocking playback runtime.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32--C3%20%7C%20ESP32%20%7C%20ESP8266-green.svg)]()
[![Python](https://img.shields.io/badge/Python-3.9%2B-blue.svg)]()

---

## Overview

**Animotion v0.1** is a lightweight, zero-dependency embedded animation workflow designed for resource-constrained microcontrollers such as the **ESP32-C3 SuperMini** paired with monochrome OLED displays (SSD1306 128×64).

### Key Features
- **Offline GIF Converter:** Converts animated GIFs into row-major 1-bit MSB-first PROGMEM byte arrays compatible with `Adafruit_GFX::drawBitmap()`.
- **Timing Preservation & Overrides:** Preserves individual GIF frame delays or overrides them with a configurable `--fps` or `--delay-ms`.
- **Zero-Allocation Runtime Player:** Minimal C++ engine (`engine/include/animotion.h`) that plays animations non-blockingly using `millis()`.
- **Display-Independent:** Renders frames via a decoupled callback or `IDisplayAdapter` interface.
- **Built-in Sample Generator:** Create test animations immediately without downloading external assets.

---

## Architecture & Bit Format

The converter packs monochrome pixels into scanlines:
- **Scanline padding:** Each row is padded to a whole byte boundary: `row_bytes = (width + 7) / 8`.
- **Bit order:** MSB-first (`0x80` is the leftmost pixel of each byte).
- **Polarity:** `1` = Pixel ON (White), `0` = Pixel OFF (Black). (Inversion supported via `--invert`).
- **Memory:** All frames are stored in Flash ROM (`PROGMEM`), requiring **0 bytes of dynamic heap** during playback.

---

## Quickstart

### 1. Installation

Clone the repository and install Pillow:

```bash
git clone https://github.com/AlcesOzdst/animotion.git
cd animotion
pip install -r requirements.txt
```

### 2. Generate or Convert an Animation

Generate the built-in sample GIF (rotating radar and telemetry HUD) and convert it to a C++ header in one step:

```bash
python converter/animotion.py --generate-sample assets/sample.gif -o examples/esp32c3-ssd1306/src/animation.h --var-name animation
```

Or convert any custom GIF:

```bash
python converter/animotion.py my_animation.gif -o animation.h -W 128 -H 64 --fps 15
```

#### CLI Options

| Flag | Default | Description |
|---|---|---|
| `input` | — | Path to source GIF file |
| `-o, --output` | `<input>.h` | Destination C++ header file |
| `-W, --width` | `128` | Target width in pixels |
| `-H, --height` | `64` | Target height in pixels |
| `--fps` | *from GIF* | Target playback framerate (e.g. `15.0`) |
| `--delay-ms` | *from GIF* | Uniform per-frame delay override in milliseconds |
| `--threshold` | `128` | Binarization cutoff threshold (0–255) |
| `--dither` | `False` | Enable Floyd-Steinberg dithering |
| `--invert` | `False` | Invert pixels (`1`=black, `0`=white) |
| `--fit` | `stretch` | Scaling mode: `stretch` or `contain` (aspect ratio preserved) |
| `--var-name` | *from file* | C/C++ variable identifier prefix |
| `--generate-sample` | — | Generate a demo animated GIF asset |

---

## Target Hardware & Wiring

Targeted at the **ESP32-C3 SuperMini** and standard **SSD1306 0.96" I2C OLED (128×64)**:

| ESP32-C3 SuperMini Pin | SSD1306 OLED Pin | Description |
|---|---|---|
| `3.3V` | `VCC` | Power (3.3V) |
| `GND` | `GND` | Ground |
| `GPIO 8` | `SDA` | I2C Data (default hardware I2C) |
| `GPIO 9` | `SCL` | I2C Clock (default hardware I2C) |

*(Note: Some alternate ESP32-C3 pinouts expose I2C on GPIO 4/5. Adjust `I2C_SDA_PIN` / `I2C_SCL_PIN` in `main.cpp` accordingly).*

---

## Firmware Integration

### Minimal Example (`main.cpp`)

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "animotion.h"    // Reusable player engine
#include "animation.h"    // Generated animation header

Adafruit_SSD1306 display(128, 64, &Wire, -1);
animotion::Player player;

// Decoupled frame renderer callback
void renderFrame(const uint8_t* bitmap, uint16_t w, uint16_t h, uint16_t frame, void* user_data) {
    Adafruit_SSD1306* disp = static_cast<Adafruit_SSD1306*>(user_data);
    disp->clearDisplay();
    disp->drawBitmap(0, 0, bitmap, w, h, SSD1306_WHITE);
    disp->display();
}

void setup() {
    Wire.begin(8, 9); // SDA=GPIO8, SCL=GPIO9
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

    // Initialize player with generated data and callback
    player.begin(&ANIMATION_DATA, renderFrame, &display);
}

void loop() {
    // Non-blocking update driven by millis() — NO delay()
    player.update(millis());
}
```

### Running with PlatformIO

```bash
cd examples/esp32c3-ssd1306
pio run --target upload
```

---

## Verification & Testing

Animotion includes unit tests for both offline conversion and the C++ engine:

```bash
# Run Python converter tests
python -m unittest discover -s tests -v

# Run native C++ engine test (requires g++)
g++ -std=c++11 -Iengine/include tests/test_engine.cpp -o tests/test_engine.exe
.\tests\test_engine.exe
```

---

## Roadmap

- **Target-Aware Compression:** Run-Length Encoding (RLE) and temporal frame differencing (XOR / delta encoding) for large frame counts.
- **Display Drivers:** Built-in adapters for SH1106, ST7789, and U8g2.
- **MP4 / Video Ingestion:** Lightweight video-to-GIF/bitmap pipeline.

---

## License

MIT License. Copyright (c) 2026 Parth Doshi. See [LICENSE](LICENSE) for details.
