/**
 * @file main.cpp
 * @brief Animotion v0.1 Playback Example for ESP32-C3 SuperMini + SSD1306 OLED
 *
 * Demonstrates non-blocking playback of animations converted from GIF using
 * Animotion's display-independent runtime player.
 *
 * Hardware:
 *   - Board: ESP32-C3 SuperMini (or compatible ESP32 board)
 *   - Display: SSD1306 0.96" Monochrome OLED (128x64 pixels, I2C)
 *   - Wiring:
 *       VCC -> 3.3V
 *       GND -> GND
 *       SDA -> GPIO 8 (default hardware I2C on ESP32-C3)
 *       SCL -> GPIO 9 (default hardware I2C on ESP32-C3)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Core Animotion engine header
#include "animotion.h"

// Generated animation header (exported from converter/animotion.py)
#include "animation.h"

// Display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_I2C_ADDRESS 0x3C

// Pinout for ESP32-C3 SuperMini
// Note: Some third-party boards route I2C to GPIO 4 (SDA) and GPIO 5 (SCL).
// Adjust these defines if your board uses alternate pins.
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// Display instance
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Reusable, zero-allocation Animotion player instance
animotion::Player player;

/**
 * @brief Display adapter callback.
 * Animotion calls this function whenever a new frame should be rendered.
 * This decouples the playback engine from any specific display driver.
 *
 * @param bitmap Pointer to 1-bit row-major MSB-first frame data in PROGMEM
 * @param width Bitmap width in pixels
 * @param height Bitmap height in pixels
 * @param frame_index 0-based index of the frame being drawn
 * @param user_data User pointer passed to player.begin() (Adafruit_SSD1306 instance)
 */
void onRenderFrame(const uint8_t* bitmap, uint16_t width, uint16_t height, uint16_t frame_index, void* user_data) {
    Adafruit_SSD1306* disp = static_cast<Adafruit_SSD1306*>(user_data);

    // Clear display buffer
    disp->clearDisplay();

    // Draw the 1-bit monochrome frame bitmap at (0, 0)
    // Animotion packs rows MSB-first, perfectly matching Adafruit_GFX drawBitmap.
    disp->drawBitmap(0, 0, bitmap, width, height, SSD1306_WHITE);

    // Push buffer to physical OLED over I2C
    disp->display();
}

void setup() {
    Serial.begin(115200);
    // Brief settling time for USB CDC / Serial on ESP32-C3
    delay(400);

    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("  Animotion v0.1 — ESP32-C3 SuperMini   "));
    Serial.println(F("========================================"));

    // Initialize I2C bus with ESP32-C3 pins
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Initialize SSD1306 display
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_I2C_ADDRESS)) {
        Serial.println(F("[ERROR] SSD1306 allocation failed. Check I2C address (0x3C) and wiring!"));
        for (;;) {
            delay(1000);
        }
    }

    display.clearDisplay();
    display.display();
    Serial.println(F("[OK] SSD1306 display initialized."));

    // Initialize player with generated animation data, adapter callback, and display pointer
    player.begin(&ANIMATION_DATA, onRenderFrame, &display);
    player.setLoop(true);

    Serial.printf("[OK] Playing animation: %d frames (%dx%d), delay=%d ms\n",
                  ANIMATION_FRAME_COUNT,
                  ANIMATION_WIDTH,
                  ANIMATION_HEIGHT,
                  ANIMATION_DEFAULT_DELAY_MS);
}

void loop() {
    // Non-blocking frame update driven by current timestamp (millis()).
    // Returns true on the exact loop iteration when a new frame is pushed.
    player.update(millis());

    // NOTE:
    // Do NOT call delay() in this loop!
    // The main loop remains 100% non-blocking and free for other sensor
    // reading, buttons, or networking tasks.
}
