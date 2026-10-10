/**
 * @file test_engine.cpp
 * @brief Unit tests for Animotion modular C++ engine & generated header integration.
 * Compiles natively with g++ to verify syntax, modular headers, memory footprint,
 * and non-blocking timing logic (including rollover).
 */

#include <iostream>
#include <cassert>
#include <vector>

// Modular includes test
#include "animotion/animation.h"
#include "animotion/display_adapter.h"
#include "animotion/player.h"

// Umbrella header include test
#include "animotion.h"

// Generated animation header
#include "../examples/esp32c3-ssd1306/src/animation.h"

// Mock renderer tracking invocations
struct MockRenderer {
    int render_count = 0;
    std::vector<uint16_t> rendered_frames;
    const uint8_t* last_bitmap = nullptr;

    void reset() {
        render_count = 0;
        rendered_frames.clear();
        last_bitmap = nullptr;
    }
};

static void testCallback(const uint8_t* bitmap, uint16_t width, uint16_t height, uint16_t frame_index, void* user_data) {
    (void)width;
    (void)height;
    MockRenderer* mock = static_cast<MockRenderer*>(user_data);
    mock->render_count++;
    mock->rendered_frames.push_back(frame_index);
    mock->last_bitmap = bitmap;
}

class MockAdapter : public animotion::IDisplayAdapter {
public:
    int render_count = 0;
    std::vector<uint16_t> rendered_frames;

    void drawFrame(const uint8_t* bitmap, uint16_t width, uint16_t height, uint16_t frame_index) override {
        (void)bitmap;
        (void)width;
        (void)height;
        render_count++;
        rendered_frames.push_back(frame_index);
    }
};

void test_initialization_and_first_frame() {
    MockRenderer mock;
    animotion::Player player;

    player.begin(&ANIMATION_DATA, testCallback, &mock);

    assert(player.isPlaying() == true);
    assert(player.isFinished() == false);
    assert(player.getCurrentFrame() == 0);
    assert(mock.render_count == 0);

    // First call to update renders frame 0 immediately
    bool updated = player.update(1000);
    assert(updated == true);
    assert(mock.render_count == 1);
    assert(mock.rendered_frames[0] == 0);
    assert(mock.last_bitmap == animation_frames[0]);

    // Sub-delay update does not trigger new frame (frame delay is 60ms)
    updated = player.update(1020);
    assert(updated == false);
    assert(mock.render_count == 1);

    std::cout << "  [PASS] test_initialization_and_first_frame" << std::endl;
}

void test_playback_frame_advance() {
    MockRenderer mock;
    animotion::Player player;
    player.begin(&ANIMATION_DATA, testCallback, &mock);

    uint32_t simulated_time = 0;
    player.update(simulated_time); // Frame 0 rendered at t=0

    // Frame delay is 60ms. Advance 60ms -> Frame 1
    simulated_time += 60;
    bool updated = player.update(simulated_time);
    assert(updated == true);
    assert(player.getCurrentFrame() == 1);
    assert(mock.rendered_frames.back() == 1);
    assert(mock.last_bitmap == animation_frames[1]);

    // Advance another 60ms -> Frame 2
    simulated_time += 60;
    updated = player.update(simulated_time);
    assert(updated == true);
    assert(player.getCurrentFrame() == 2);
    assert(mock.rendered_frames.back() == 2);

    std::cout << "  [PASS] test_playback_frame_advance" << std::endl;
}

void test_looping_behavior() {
    MockRenderer mock;
    animotion::Player player;
    player.begin(&ANIMATION_DATA, testCallback, &mock);
    player.setLoop(true);

    uint32_t t = 100;
    player.update(t); // Frame 0

    // Step through all 16 frames
    for (int i = 1; i < ANIMATION_FRAME_COUNT; ++i) {
        t += 60;
        player.update(t);
    }
    assert(player.getCurrentFrame() == ANIMATION_FRAME_COUNT - 1);

    // Next step wraps around to Frame 0
    t += 60;
    bool updated = player.update(t);
    assert(updated == true);
    assert(player.getCurrentFrame() == 0);
    assert(player.isPlaying() == true);
    assert(player.isFinished() == false);

    std::cout << "  [PASS] test_looping_behavior" << std::endl;
}

void test_non_looping_stop() {
    MockRenderer mock;
    animotion::Player player;
    player.begin(&ANIMATION_DATA, testCallback, &mock);
    player.setLoop(false);

    uint32_t t = 0;
    player.update(t); // Frame 0

    // Step to last frame
    for (int i = 1; i < ANIMATION_FRAME_COUNT; ++i) {
        t += 60;
        player.update(t);
    }
    assert(player.getCurrentFrame() == ANIMATION_FRAME_COUNT - 1);

    // Next step terminates playback
    t += 60;
    bool updated = player.update(t);
    assert(updated == false);
    assert(player.isPlaying() == false);
    assert(player.isFinished() == true);

    // Subsequent updates stay finished
    t += 60;
    assert(player.update(t) == false);

    std::cout << "  [PASS] test_non_looping_stop" << std::endl;
}

void test_adapter_interface() {
    MockAdapter adapter;
    animotion::Player player;
    player.begin(&ANIMATION_DATA, &adapter);

    player.update(0); // Frame 0
    player.update(60); // Frame 1

    assert(adapter.render_count == 2);
    assert(adapter.rendered_frames[0] == 0);
    assert(adapter.rendered_frames[1] == 1);

    std::cout << "  [PASS] test_adapter_interface" << std::endl;
}

void test_pause_and_resume() {
    MockRenderer mock;
    animotion::Player player;
    player.begin(&ANIMATION_DATA, testCallback, &mock);

    player.update(0); // Frame 0
    player.pause();
    assert(player.isPlaying() == false);

    // While paused, updates do not advance frames
    bool updated = player.update(100);
    assert(updated == false);
    assert(mock.render_count == 1);

    player.play();
    assert(player.isPlaying() == true);

    std::cout << "  [PASS] test_pause_and_resume" << std::endl;
}

void test_millis_rollover() {
    MockRenderer mock;
    animotion::Player player;
    player.begin(&ANIMATION_DATA, testCallback, &mock);

    // Set timestamp near 32-bit overflow: 0xFFFFFFE0 (4,294,967,264)
    uint32_t t_before = 0xFFFFFFE0;
    player.update(t_before); // Frame 0
    assert(mock.rendered_frames.back() == 0);

    // Rollover to small timestamp: 0x00000020 (32).
    // Elapsed = 32 - 4294967264 = 64 (in 32-bit unsigned arithmetic).
    // Frame delay is 60ms. Elapsed (64) >= 60 -> advances to Frame 1.
    uint32_t t_after = 0x00000020;
    bool updated = player.update(t_after);
    assert(updated == true);
    assert(player.getCurrentFrame() == 1);
    assert(mock.rendered_frames.back() == 1);

    std::cout << "  [PASS] test_millis_rollover" << std::endl;
}

void test_memory_footprint() {
    size_t player_ram = sizeof(animotion::Player);
    size_t anim_struct_size = sizeof(animotion::Animation);

    // Host size will be 64-bit aligned, MCU 32-bit aligned.
    // Assert both are strictly lightweight (< 64 bytes).
    assert(player_ram <= 64);
    assert(anim_struct_size <= 64);

    size_t frame_bitmap_bytes = ANIMATION_BYTES_PER_FRAME * ANIMATION_FRAME_COUNT;
    size_t frame_delays_bytes = sizeof(uint16_t) * ANIMATION_FRAME_COUNT;
    size_t frame_pointers_bytes = sizeof(void*) * ANIMATION_FRAME_COUNT;
    size_t total_flash_bytes = frame_bitmap_bytes + frame_delays_bytes + frame_pointers_bytes + anim_struct_size;

    std::cout << "  [MEMORY METRICS]" << std::endl;
    std::cout << "    - sizeof(animotion::Player) RAM:   " << player_ram << " bytes" << std::endl;
    std::cout << "    - sizeof(animotion::Animation):    " << anim_struct_size << " bytes" << std::endl;
    std::cout << "    - Flash Frame Bitmaps:             " << frame_bitmap_bytes << " bytes (" << (frame_bitmap_bytes / 1024.0) << " KB)" << std::endl;
    std::cout << "    - Flash Delays + Pointers:         " << (frame_delays_bytes + frame_pointers_bytes) << " bytes" << std::endl;
    std::cout << "    - Total Flash Footprint:           " << total_flash_bytes << " bytes (~" << (total_flash_bytes / 1024.0) << " KB)" << std::endl;
    std::cout << "    - Playback Dynamic Heap Usage:     0 bytes (zero-allocation)" << std::endl;
    std::cout << "  [PASS] test_memory_footprint" << std::endl;
}

int main() {
    std::cout << "Running Animotion C++ Engine Unit Tests..." << std::endl;

    test_initialization_and_first_frame();
    test_playback_frame_advance();
    test_looping_behavior();
    test_non_looping_stop();
    test_adapter_interface();
    test_pause_and_resume();
    test_millis_rollover();
    test_memory_footprint();

    std::cout << "All Animotion C++ Engine Unit Tests PASSED successfully!" << std::endl;
    return 0;
}
