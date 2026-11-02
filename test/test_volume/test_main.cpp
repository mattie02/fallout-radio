// VolumeKnob: raw pot readings -> steady level -> perceptual gain.

#include <unity.h>

#include "core/VolumeKnob.h"

using namespace radio;

void setUp() {}
void tearDown() {}

// Feeds the same reading until the smoothing settles.
static void settle(VolumeKnob& k, float raw) {
    for (int i = 0; i < 100; ++i) k.update(raw);
}

void test_no_reading_means_full_gain() {
    VolumeKnob k;
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, k.level());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, k.gain());
}

void test_first_reading_is_taken_as_is() {
    VolumeKnob k;
    TEST_ASSERT_TRUE(k.update(0.5f));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, k.level());
}

void test_ends_are_hard_zero_and_full() {
    VolumeKnob k;
    settle(k, 0.01f);  // inside the bottom dead zone
    TEST_ASSERT_EQUAL_FLOAT(0.0f, k.level());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, k.gain());
    settle(k, 0.99f);  // inside the top dead zone
    TEST_ASSERT_EQUAL_FLOAT(1.0f, k.level());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, k.gain());
}

void test_noise_is_ignored() {
    VolumeKnob k;
    settle(k, 0.5f);
    const float level = k.level();
    // ADC jitter of a few tenths of a percent never changes the level.
    for (int i = 0; i < 200; ++i) {
        TEST_ASSERT_FALSE(k.update(i % 2 ? 0.504f : 0.496f));
    }
    TEST_ASSERT_EQUAL_FLOAT(level, k.level());
}

void test_real_turns_are_followed() {
    VolumeKnob k;
    settle(k, 0.5f);
    settle(k, 0.6f);
    TEST_ASSERT_FLOAT_WITHIN(VolumeKnob::kHysteresis, 0.6f, k.level());
}

void test_curve_is_logarithmic() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, VolumeKnob::curve(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, VolumeKnob::curve(1.0f));
    // Halfway is -20 dB with a 40 dB range.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.1f, VolumeKnob::curve(0.5f));
    // Equal turns give equal ratios.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, VolumeKnob::curve(0.2f) / VolumeKnob::curve(0.1f),
                             VolumeKnob::curve(0.9f) / VolumeKnob::curve(0.8f));
    float prev = 0;
    for (int i = 1; i <= 100; ++i) {
        const float g = VolumeKnob::curve(i / 100.0f);
        TEST_ASSERT_TRUE(g > prev);
        prev = g;
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_reading_means_full_gain);
    RUN_TEST(test_first_reading_is_taken_as_is);
    RUN_TEST(test_ends_are_hard_zero_and_full);
    RUN_TEST(test_noise_is_ignored);
    RUN_TEST(test_real_turns_are_followed);
    RUN_TEST(test_curve_is_logarithmic);
    return UNITY_END();
}
