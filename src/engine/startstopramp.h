#pragma once

#include "audio/types.h"
#include "control/controlvalue.h"

/// Ramps the deck speed down when playback stops and back up when it starts
/// again, so a deck spins down and up like a turntable instead of cutting the
/// sound off. Playback stops either because the deck was paused or because the
/// platter is held down, like a hand resting on a record. Both durations are
/// global user settings, a duration of 0 s disables the corresponding ramp.
class StartStopRamp {
  public:
    /// Returns the speed the current buffer has to be played at.
    /// `speed` is the speed requested by the rate controls, `paused` the state
    /// of the play button, `platterHeld` whether the platter is held down,
    /// `instant` suppresses both ramps (cue previews and vinyl control start
    /// and stop instantly) and `seeked` tells that the play position was moved
    /// for this buffer, which ends a running spin down.
    /// `pScratching` is the caller's scratching flag. Scratching overrides the
    /// ramp, and the flag is set while a ramp is running so that the linear
    /// scaler is used and the pitch follows the speed, like on a turntable.
    double process(double speed,
            bool paused,
            bool platterHeld,
            bool instant,
            bool seeked,
            int bufferSamples,
            mixxx::audio::SampleRate sampleRate,
            bool* pScratching);

    /// Time the deck takes to spin down to a stop.
    static void setBrakeTime(double seconds);
    static double getBrakeTime();
    /// Time the deck takes to spin up to full speed.
    static void setSoftStartTime(double seconds);
    static double getSoftStartTime();

    static constexpr double kMaxRampTime = 10.0;

  private:
    // Deck speed as a fraction of the requested speed: 0.0 stopped, 1.0 full speed.
    double m_factor{0.0};
    // Speed the deck was running at before it stopped.
    double m_playSpeed{0.0};

    static ControlValueAtomic<double> s_brakeTime;
    static ControlValueAtomic<double> s_softStartTime;
};
