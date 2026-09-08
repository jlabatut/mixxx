#include "engine/startstopramp.h"

#include <gtest/gtest.h>

namespace {

constexpr auto kSampleRate = mixxx::audio::SampleRate(44100);
// 4410 frames, one tenth of a second at 44.1 kHz.
constexpr int kBufferSamples = 8820;

class StartStopRampTest : public testing::Test {
  protected:
    void SetUp() override {
        StartStopRamp::setBrakeTime(0.0);
        StartStopRamp::setSoftStartTime(0.0);
    }

    double process(double speed, bool paused, bool platterHeld = false, bool seeked = false) {
        m_scratching = false;
        return m_ramp.process(speed,
                paused,
                platterHeld,
                /*instant*/ false,
                seeked,
                kBufferSamples,
                kSampleRate,
                &m_scratching);
    }

    // Brings the deck to full speed, whatever soft start time is configured.
    void spinUpToFullSpeed() {
        for (int i = 0; i < 20; ++i) {
            process(1.0, false);
        }
        ASSERT_NEAR(1.0, process(1.0, false), 1e-9);
    }

    StartStopRamp m_ramp;
    bool m_scratching{false};
};

TEST_F(StartStopRampTest, WithoutRampTimesTheSpeedPassesThrough) {
    EXPECT_DOUBLE_EQ(1.0, process(1.0, false));
    EXPECT_FALSE(m_scratching);
    EXPECT_DOUBLE_EQ(0.0, process(0.0, true));
    EXPECT_FALSE(m_scratching);
    // A jog scrub while the deck is stopped is left alone.
    EXPECT_DOUBLE_EQ(-2.0, process(-2.0, true));
    EXPECT_FALSE(m_scratching);
}

TEST_F(StartStopRampTest, SoftStartRampsUpToTheRequestedSpeed) {
    StartStopRamp::setSoftStartTime(0.5); // five buffers
    for (int i = 1; i < 5; ++i) {
        EXPECT_NEAR(i * 0.2, process(1.0, false), 1e-9);
        EXPECT_TRUE(m_scratching);
    }
    EXPECT_NEAR(1.0, process(1.0, false), 1e-9);
    EXPECT_FALSE(m_scratching);
    // The ramp is over, the rate controls are in charge again.
    EXPECT_DOUBLE_EQ(1.03, process(1.03, false));
}

TEST_F(StartStopRampTest, BrakeSpinsDownFromThePlayingSpeed) {
    StartStopRamp::setBrakeTime(0.5); // five buffers
    ASSERT_DOUBLE_EQ(1.03, process(1.03, false));

    for (int i = 4; i > 0; --i) {
        EXPECT_NEAR(i * 0.2 * 1.03, process(0.0, true), 1e-9);
        EXPECT_TRUE(m_scratching);
    }
    EXPECT_DOUBLE_EQ(0.0, process(0.0, true));
    EXPECT_FALSE(m_scratching);
}

TEST_F(StartStopRampTest, SeekEndsTheSpinDown) {
    StartStopRamp::setBrakeTime(0.5);
    ASSERT_DOUBLE_EQ(1.0, process(1.0, false));
    ASSERT_NEAR(0.8, process(0.0, true), 1e-9);

    // Jumping to a cue stops the deck right away.
    EXPECT_DOUBLE_EQ(0.0, process(0.0, true, false, /*seeked*/ true));
    EXPECT_FALSE(m_scratching);
    EXPECT_DOUBLE_EQ(0.0, process(0.0, true));
}

TEST_F(StartStopRampTest, JogScrubEndsTheSpinDown) {
    StartStopRamp::setBrakeTime(0.5);
    ASSERT_DOUBLE_EQ(1.0, process(1.0, false));
    ASSERT_NEAR(0.8, process(0.0, true), 1e-9);

    EXPECT_DOUBLE_EQ(3.0, process(3.0, true));
    EXPECT_FALSE(m_scratching);
}

TEST_F(StartStopRampTest, PlayingAgainRampsUpFromTheCurrentSpeed) {
    StartStopRamp::setBrakeTime(1.0);   // ten buffers
    StartStopRamp::setSoftStartTime(0.5); // five buffers
    for (int i = 0; i < 5; ++i) {
        process(1.0, false);
    }
    ASSERT_NEAR(1.0, process(1.0, false), 1e-9);

    for (int i = 9; i > 5; --i) {
        ASSERT_NEAR(i * 0.1, process(0.0, true), 1e-9);
    }

    // Playing again picks the ramp up where the spin down left it.
    EXPECT_NEAR(0.8, process(1.0, false), 1e-9);
    EXPECT_TRUE(m_scratching);
    EXPECT_NEAR(1.0, process(1.0, false), 1e-9);
    EXPECT_FALSE(m_scratching);
}

TEST_F(StartStopRampTest, ScratchingAndPreviewsAreInstant) {
    StartStopRamp::setBrakeTime(0.5);
    StartStopRamp::setSoftStartTime(0.5);

    // A cue preview starts at full speed and stops right away.
    EXPECT_DOUBLE_EQ(1.0,
            m_ramp.process(1.0,
                    /*paused*/ false,
                    /*platterHeld*/ false,
                    /*instant*/ true,
                    /*seeked*/ false,
                    kBufferSamples,
                    kSampleRate,
                    &m_scratching));
    EXPECT_DOUBLE_EQ(0.0,
            m_ramp.process(0.0,
                    /*paused*/ true,
                    /*platterHeld*/ false,
                    /*instant*/ true,
                    /*seeked*/ false,
                    kBufferSamples,
                    kSampleRate,
                    &m_scratching));

    // Scratching is not ramped either.
    m_scratching = true;
    EXPECT_DOUBLE_EQ(-1.5,
            m_ramp.process(-1.5,
                    /*paused*/ false,
                    /*platterHeld*/ false,
                    /*instant*/ false,
                    /*seeked*/ false,
                    kBufferSamples,
                    kSampleRate,
                    &m_scratching));
    EXPECT_TRUE(m_scratching);
}

TEST_F(StartStopRampTest, HeldPlatterSpinsAPlayingDeckDownAndBackUp) {
    StartStopRamp::setBrakeTime(0.5);     // five buffers
    StartStopRamp::setSoftStartTime(0.5); // five buffers
    spinUpToFullSpeed();

    // The deck keeps playing, the platter is what stops. The requested speed
    // stays at 1.0 throughout.
    for (int i = 4; i > 0; --i) {
        EXPECT_NEAR(i * 0.2, process(1.0, false, /*platterHeld*/ true), 1e-9);
        EXPECT_TRUE(m_scratching);
    }
    // Held and stopped: silent, and the requested speed must not leak through.
    EXPECT_DOUBLE_EQ(0.0, process(1.0, false, /*platterHeld*/ true));
    EXPECT_FALSE(m_scratching);

    // Releasing the platter spins the deck back up.
    for (int i = 1; i < 5; ++i) {
        EXPECT_NEAR(i * 0.2, process(1.0, false), 1e-9);
        EXPECT_TRUE(m_scratching);
    }
    EXPECT_NEAR(1.0, process(1.0, false), 1e-9);
    EXPECT_FALSE(m_scratching);
}

TEST_F(StartStopRampTest, HeldPlatterDoesNotStartAPausedDeck) {
    StartStopRamp::setBrakeTime(0.5);
    StartStopRamp::setSoftStartTime(0.5);
    ASSERT_DOUBLE_EQ(0.0, process(0.0, true));

    EXPECT_DOUBLE_EQ(0.0, process(0.0, true, /*platterHeld*/ true));
    // Releasing it leaves the deck paused, not playing.
    EXPECT_DOUBLE_EQ(0.0, process(0.0, true));
    EXPECT_FALSE(m_scratching);
}

TEST_F(StartStopRampTest, PausingWhileThePlatterIsHeldKeepsTheDeckStopped) {
    StartStopRamp::setBrakeTime(0.5);
    StartStopRamp::setSoftStartTime(0.5);
    spinUpToFullSpeed();
    ASSERT_NEAR(0.8, process(1.0, false, /*platterHeld*/ true), 1e-9);

    // Hitting pause mid spin down continues it.
    EXPECT_NEAR(0.6, process(0.0, true, /*platterHeld*/ true), 1e-9);
    // Releasing the platter on a paused deck must not spin it up again.
    EXPECT_NEAR(0.4, process(0.0, true), 1e-9);
    EXPECT_NEAR(0.2, process(0.0, true), 1e-9);
    EXPECT_DOUBLE_EQ(0.0, process(0.0, true));
}

} // namespace
