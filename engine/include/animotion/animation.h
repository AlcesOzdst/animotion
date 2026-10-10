#ifndef ANIMOTION_ANIMATION_H
#define ANIMOTION_ANIMATION_H

#include <stdint.h>
#include <stdbool.h>

// Platform-aware PROGMEM and memory-access macro abstraction
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO)
    #include <pgmspace.h>
#else
    #ifndef PROGMEM
        #define PROGMEM
    #endif
    #ifndef pgm_read_byte
        #define pgm_read_byte(addr) (*(const uint8_t*)(addr))
    #endif
    #ifndef pgm_read_word
        #define pgm_read_word(addr) (*(const uint16_t*)(addr))
    #endif
    #ifndef pgm_read_dword
        #define pgm_read_dword(addr) (*(const uint32_t*)(addr))
    #endif
    #ifndef pgm_read_ptr
        #define pgm_read_ptr(addr) (*(const void* const*)(addr))
    #endif
#endif

namespace animotion {

#ifndef ANIMOTION_STRUCT_DEFINED
#define ANIMOTION_STRUCT_DEFINED
/**
 * @brief Core animation asset metadata and flash frame pointers.
 * Placed in Flash ROM (PROGMEM) on microcontrollers.
 */
struct Animation {
    const uint8_t* const* frames;         // Array of pointers to PROGMEM frame bitmaps
    const uint16_t* frame_delays_ms;      // Optional per-frame delay array in ms (PROGMEM)
    uint16_t frame_count;                 // Total number of frames
    uint16_t width;                       // Frame width in pixels
    uint16_t height;                      // Frame height in pixels
    uint16_t default_delay_ms;            // Fallback delay if frame_delays_ms is null
    bool loop;                            // Default looping preference
};
#endif

/**
 * @brief Reads a pointer to a frame bitmap stored in PROGMEM.
 * Uses architecture-appropriate access macros (e.g. pgm_read_ptr on AVR/ESP).
 */
inline const uint8_t* readFramePointer(const uint8_t* const* frameList, uint16_t index) {
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO)
    return (const uint8_t*)pgm_read_ptr(&frameList[index]);
#else
    return frameList[index];
#endif
}

/**
 * @brief Reads a 16-bit frame delay stored in PROGMEM.
 * Uses architecture-appropriate access macros (e.g. pgm_read_word on AVR/ESP).
 */
inline uint16_t readFrameDelay(const uint16_t* delayList, uint16_t index, uint16_t defaultDelay = 100) {
    if (!delayList) {
        return (defaultDelay > 0) ? defaultDelay : 100;
    }
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO)
    uint16_t d = pgm_read_word(&delayList[index]);
#else
    uint16_t d = delayList[index];
#endif
    return (d > 0) ? d : ((defaultDelay > 0) ? defaultDelay : 100);
}

} // namespace animotion

#endif // ANIMOTION_ANIMATION_H
