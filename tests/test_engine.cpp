/**
 * @file test_engine.cpp
 * @brief Unit tests for Animotion C++ Player Engine & generated header integration.
 * Compiles natively with g++ to verify syntax, memory layout, and timing logic.
 */

#include <iostream>
#include <cassert>
#include <vector>
#include "animotion.h"
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

int main() {
    std::cout << "Running Animotion C++ Engine Unit Tests..." << std::endl;

    test_initialization_and_first_frame();
    test_playback_frame_advance();
    test_looping_behavior();
    test_non_looping_stop();
    test_adapter_interface();
    test_pause_and_resume();

    std::cout << "All Animotion C++ Engine Unit Tests PASSED successfully!" << std::endl;
    return 0;
}
