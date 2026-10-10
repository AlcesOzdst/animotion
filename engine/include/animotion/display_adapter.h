#ifndef ANIMOTION_DISPLAY_ADAPTER_H
#define ANIMOTION_DISPLAY_ADAPTER_H

#include <stdint.h>
#include "animation.h"

namespace animotion {

/**
 * @brief Function pointer signature for decoupled, display-independent rendering.
 *
 * @param bitmap Pointer to 1-bit row-major MSB-first frame data in flash/PROGMEM
 * @param width Animation frame width in pixels
 * @param height Animation frame height in pixels
 * @param frame_index 0-based frame index currently being rendered
 * @param user_data User context pointer (e.g. pointer to Adafruit_SSD1306 instance)
 */
typedef void (*RenderCallback)(const uint8_t* bitmap, uint16_t width, uint16_t height, uint16_t frame_index, void* user_data);

/**
 * @brief Abstract display adapter interface for object-oriented integrations.
 */
class IDisplayAdapter {
public:
    virtual ~IDisplayAdapter() {}

    /**
     * @brief Called by the player whenever a new frame is scheduled for rendering.
     *
     * @param bitmap Direct pointer to frame bitmap in Flash/PROGMEM
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param frame_index Current frame index
     */
    virtual void drawFrame(const uint8_t* bitmap, uint16_t width, uint16_t height, uint16_t frame_index) = 0;
};

} // namespace animotion

#endif // ANIMOTION_DISPLAY_ADAPTER_H
