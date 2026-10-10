#ifndef ANIMOTION_H
#define ANIMOTION_H

#include <stdint.h>
#include <stdbool.h>

// Platform-aware PROGMEM macro abstraction
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
 * @brief Core metadata and frame pointer container for an animation.
 * Typically stored in Flash/PROGMEM.
 */
struct Animation {
    const uint8_t* const* frames;         // Array of pointers to PROGMEM frame bitmaps
    const uint16_t* frame_delays_ms;      // Optional per-frame delay array in ms (PROGMEM)
    uint16_t frame_count;                 // Total number of frames
    uint16_t width;                       // Frame width in pixels
    uint16_t height;                      // Frame height in pixels
    uint16_t default_delay_ms;            // Fallback delay if frame_delays_ms is null
    bool loop;                            // Default loop behavior
};
#endif

/**
 * @brief Safely reads a pointer to a frame bitmap stored in PROGMEM.
 */
inline const uint8_t* readFramePointer(const uint8_t* const* frameList, uint16_t index) {
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO)
    return (const uint8_t*)pgm_read_ptr(&frameList[index]);
#else
    return frameList[index];
#endif
}

/**
 * @brief Safely reads a 16-bit frame delay stored in PROGMEM.
 */
inline uint16_t readFrameDelay(const uint16_t* delayList, uint16_t index, uint16_t defaultDelay = 100) {
    if (!delayList) {
        return defaultDelay;
    }
#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO)
    return pgm_read_word(&delayList[index]);
#else
    return delayList[index];
#endif
}

/**
 * @brief Function pointer signature for display-independent rendering.
 *
 * @param bitmap Pointer to 1-bit row-major MSB-first frame data in PROGMEM
 * @param width Animation width in pixels
 * @param height Animation height in pixels
 * @param frame_index 0-based frame index currently being rendered
 * @param user_data User-provided context pointer (e.g. pointer to Adafruit_SSD1306)
 */
typedef void (*RenderCallback)(const uint8_t* bitmap, uint16_t width, uint16_t height, uint16_t frame_index, void* user_data);

/**
 * @brief Display adapter interface for object-oriented integrations.
 */
class IDisplayAdapter {
public:
    virtual ~IDisplayAdapter() {}

    /**
     * @brief Called by the player whenever a new frame is ready to render.
     */
    virtual void drawFrame(const uint8_t* bitmap, uint16_t width, uint16_t height, uint16_t frame_index) = 0;
};

/**
 * @brief Lightweight, non-blocking animation playback engine.
 *
 * Features:
 * - Zero dynamic memory allocations in playback loop.
 * - Non-blocking timing driven by millis().
 * - Works with any display library via callback or IDisplayAdapter.
 * - Handles variable frame rates, single playback, and looping.
 */
class Player {
public:
    Player()
        : anim_(nullptr),
          callback_(nullptr),
          adapter_(nullptr),
          user_data_(nullptr),
          current_frame_(0),
          last_update_ms_(0),
          is_playing_(false),
          loop_(true),
          is_finished_(false),
          has_started_(false) {}

    /**
     * @brief Initialize player with a functional render callback.
     */
    void begin(const Animation* anim, RenderCallback callback, void* user_data = nullptr) {
        anim_ = anim;
        callback_ = callback;
        adapter_ = nullptr;
        user_data_ = user_data;
        reset();
    }

    /**
     * @brief Initialize player with an IDisplayAdapter interface.
     */
    void begin(const Animation* anim, IDisplayAdapter* adapter) {
        anim_ = anim;
        callback_ = nullptr;
        adapter_ = adapter;
        user_data_ = nullptr;
        reset();
    }

    /**
     * @brief Swap active animation without changing callback or adapter.
     */
    void setAnimation(const Animation* anim) {
        anim_ = anim;
        reset();
    }

    /**
     * @brief Reset playback to frame 0.
     */
    void reset() {
        current_frame_ = 0;
        last_update_ms_ = 0;
        is_finished_ = false;
        has_started_ = false;
        if (anim_ && anim_->frame_count > 0) {
            loop_ = anim_->loop;
            is_playing_ = true;
        } else {
            is_playing_ = false;
        }
    }

    /**
     * @brief Resume or start playback.
     */
    void play() {
        if (anim_ && anim_->frame_count > 0) {
            is_playing_ = true;
            is_finished_ = false;
        }
    }

    /**
     * @brief Pause playback at the current frame.
     */
    void pause() {
        is_playing_ = false;
    }

    /**
     * @brief Stop playback and rewind to frame 0.
     */
    void stop() {
        is_playing_ = false;
        current_frame_ = 0;
        last_update_ms_ = 0;
        has_started_ = false;
    }

    /**
     * @brief Rewind to frame 0 and begin playing.
     */
    void restart() {
        reset();
        play();
    }

    /**
     * @brief Override looping behavior.
     */
    void setLoop(bool loop) {
        loop_ = loop;
    }

    bool isPlaying() const { return is_playing_; }
    bool isFinished() const { return is_finished_; }
    bool isLooping() const { return loop_; }
    uint16_t getCurrentFrame() const { return current_frame_; }

    /**
     * @brief Non-blocking update cycle.
     * Call this inside Arduino's loop() with the current millisecond timestamp (millis()).
     *
     * @param now_ms Current timestamp from millis()
     * @return true if a new frame was rendered on this call, false otherwise.
     */
    bool update(uint32_t now_ms) {
        if (!is_playing_ || !anim_ || anim_->frame_count == 0) {
            return false;
        }

        // Render first frame immediately on start
        if (!has_started_) {
            has_started_ = true;
            last_update_ms_ = now_ms;
            renderCurrentFrame();
            return true;
        }

        uint16_t delay = readFrameDelay(anim_->frame_delays_ms, current_frame_, anim_->default_delay_ms);

        // Safe against 32-bit unsigned millis() rollover
        if ((uint32_t)(now_ms - last_update_ms_) >= delay) {
            current_frame_++;

            if (current_frame_ >= anim_->frame_count) {
                if (loop_) {
                    current_frame_ = 0;
                } else {
                    current_frame_ = anim_->frame_count - 1;
                    is_playing_ = false;
                    is_finished_ = true;
                    last_update_ms_ = now_ms;
                    return false;
                }
            }

            renderCurrentFrame();
            last_update_ms_ = now_ms;
            return true;
        }

        return false;
    }

private:
    void renderCurrentFrame() {
        if (!anim_ || current_frame_ >= anim_->frame_count) {
            return;
        }

        const uint8_t* frame_ptr = readFramePointer(anim_->frames, current_frame_);
        if (callback_) {
            callback_(frame_ptr, anim_->width, anim_->height, current_frame_, user_data_);
        } else if (adapter_) {
            adapter_->drawFrame(frame_ptr, anim_->width, anim_->height, current_frame_);
        }
    }

    const Animation* anim_;
    RenderCallback callback_;
    IDisplayAdapter* adapter_;
    void* user_data_;
    uint16_t current_frame_;
    uint32_t last_update_ms_;
    bool is_playing_;
    bool loop_;
    bool is_finished_;
    bool has_started_;
};

} // namespace animotion

#endif // ANIMOTION_H
