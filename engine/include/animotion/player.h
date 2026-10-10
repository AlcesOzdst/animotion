#ifndef ANIMOTION_PLAYER_H
#define ANIMOTION_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include "animation.h"
#include "display_adapter.h"

namespace animotion {

/**
 * @brief Lightweight, non-blocking animation playback engine.
 *
 * Characteristics:
 * - Zero dynamic heap allocations in playback loop.
 * - Millis rollover safe via unsigned difference math.
 * - Hardware and display library decoupled via callback or IDisplayAdapter.
 * - Renders directly from flash/PROGMEM pointers without intermediate RAM frame buffers.
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
     * @brief Swap active animation without altering display adapter or user data.
     */
    void setAnimation(const Animation* anim) {
        anim_ = anim;
        reset();
    }

    /**
     * @brief Assign or replace render callback.
     */
    void setCallback(RenderCallback callback, void* user_data = nullptr) {
        callback_ = callback;
        adapter_ = nullptr;
        user_data_ = user_data;
    }

    /**
     * @brief Assign or replace display adapter interface.
     */
    void setAdapter(IDisplayAdapter* adapter) {
        adapter_ = adapter;
        callback_ = nullptr;
        user_data_ = nullptr;
    }

    /**
     * @brief Reset playback to frame 0 and rearm first-frame render.
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
     * @brief Pause playback at the current frame without rewinding.
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
     * @brief Rewind to frame 0 and start playback immediately.
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
     * @brief Non-blocking playback tick.
     * Call inside the main microcontroller loop with current timestamp (e.g. millis()).
     *
     * @param now_ms Current millisecond timestamp
     * @return true if a frame was rendered on this invocation, false otherwise.
     */
    bool update(uint32_t now_ms) {
        if (!is_playing_ || !anim_ || anim_->frame_count == 0) {
            return false;
        }

        // Render first frame immediately upon starting
        if (!has_started_) {
            has_started_ = true;
            last_update_ms_ = now_ms;
            renderCurrentFrame();
            return true;
        }

        uint16_t delay = readFrameDelay(anim_->frame_delays_ms, current_frame_, anim_->default_delay_ms);

        // Rollover-safe unsigned duration comparison
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

#endif // ANIMOTION_PLAYER_H
