#include "engine/startstopramp.h"

#include <algorithm>

#include "engine/engine.h"

namespace {
// The ramp snaps to its target within this fraction of the playing speed. It
// is far below the smallest step a configurable ramp time can produce, so it
// only ever swallows the rounding error accumulated over the ramp.
constexpr double kFactorEpsilon = 1e-6;
} // namespace

ControlValueAtomic<double> StartStopRamp::s_brakeTime;
ControlValueAtomic<double> StartStopRamp::s_softStartTime;

void StartStopRamp::setBrakeTime(double seconds) {
    s_brakeTime.setValue(std::clamp(seconds, 0.0, kMaxRampTime));
}

double StartStopRamp::getBrakeTime() {
    return s_brakeTime.getValue();
}

void StartStopRamp::setSoftStartTime(double seconds) {
    s_softStartTime.setValue(std::clamp(seconds, 0.0, kMaxRampTime));
}

double StartStopRamp::getSoftStartTime() {
    return s_softStartTime.getValue();
}

double StartStopRamp::process(double speed,
        bool paused,
        bool platterHeld,
        bool instant,
        bool seeked,
        int bufferSamples,
        mixxx::audio::SampleRate sampleRate,
        bool* pScratching) {
    const double bufferFrames =
            bufferSamples / static_cast<double>(mixxx::kEngineChannelCount);
    const double bufferDuration =
            sampleRate.isValid() ? bufferFrames / sampleRate.toDouble() : 0.0;
    // A paused deck ignores the platter, it has nothing to spin down and must
    // not start playing when the platter is released.
    const bool stopping = paused || platterHeld;

    if (*pScratching || instant || bufferDuration <= 0.0) {
        // Scratching and cue previews follow the requested speed. Only track
        // where that leaves the deck, so a later ramp starts from there.
        m_factor = stopping ? 0.0 : 1.0;
        if (!stopping) {
            m_playSpeed = speed;
        }
        return speed;
    }

    if (!stopping) {
        const double softStartTime = getSoftStartTime();
        if (speed == 0.0 || softStartTime <= 0.0) {
            // A deck without transport has nothing to ramp up.
            m_factor = 1.0;
        } else if (m_factor < 1.0) {
            m_factor += bufferDuration / softStartTime;
            if (m_factor > 1.0 - kFactorEpsilon) {
                m_factor = 1.0;
            } else {
                *pScratching = true;
            }
        }
        // Remember the requested speed, the spin down starts from it.
        m_playSpeed = speed;
        return speed * m_factor;
    }

    const double brakeTime = getBrakeTime();
    if (seeked || (paused && speed != 0.0) || brakeTime <= 0.0) {
        // A seek or a jog scrub moves the deck away from where the spin down
        // started, so end it right here.
        m_factor = 0.0;
    } else if (m_factor > 0.0) {
        m_factor -= bufferDuration / brakeTime;
        if (m_factor < kFactorEpsilon) {
            m_factor = 0.0;
        }
    }

    if (m_factor > 0.0) {
        *pScratching = true;
        return m_playSpeed * m_factor;
    }
    // Stopped. A paused deck can still be scrubbed with the jog, a deck with
    // the platter held down stays silent until the platter is released.
    return paused ? speed : 0.0;
}
