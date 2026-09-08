#include "effects/backends/builtin/transeffect.h"

#include <algorithm>
#include <cmath>

namespace {
// Fade at the gate edges, short enough to stay sharp, long enough to avoid clicks
constexpr double kFadeSeconds = 0.002;
// Keeps the fades from swallowing very short periods
constexpr double kMaxFadeRatio = 0.25;
constexpr double kMinFramesPerPeriod = 2.0;
} // namespace

// static
QString TransEffect::getId() {
    return "org.mixxx.effects.trans";
}

// static
EffectManifestPointer TransEffect::getManifest() {
    EffectManifestPointer pManifest(new EffectManifest());
    pManifest->setId(getId());
    pManifest->setName(QObject::tr("Trans"));
    pManifest->setShortName(QObject::tr("Trans"));
    pManifest->setAuthor("The Mixxx Team");
    pManifest->setVersion("1.0");
    pManifest->setDescription(QObject::tr(
            "Mutes and unmutes the audio in sync with the beat"));
    // Centred meta knob is a 1/2 beat period, turning it up shortens the cuts
    pManifest->setMetaknobDefault(0.5);

    EffectManifestParameterPointer period = pManifest->addParameter();
    period->setId("period");
    period->setName(QObject::tr("Period"));
    period->setShortName(QObject::tr("Period"));
    period->setDescription(QObject::tr(
            "Duration of one mute and unmute cycle\n"
            "1/16 - 4 beats if tempo is detected\n"
            "1/16 - 4 seconds if no tempo is detected"));
    period->setValueScaler(EffectManifestParameter::ValueScaler::Logarithmic);
    period->setUnitsHint(EffectManifestParameter::UnitsHint::Beats);
    period->setDefaultLinkType(EffectManifestParameter::LinkType::Linked);
    period->setDefaultLinkInversion(EffectManifestParameter::LinkInversion::Inverted);
    period->setRange(1.0 / 16, 1.0 / 2, 4);

    EffectManifestParameterPointer depth = pManifest->addParameter();
    depth->setId("depth");
    depth->setName(QObject::tr("Depth"));
    depth->setShortName(QObject::tr("Depth"));
    depth->setDescription(QObject::tr(
            "How much the audio is attenuated while the gate is closed\n"
            "Fully right: complete silence"));
    depth->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    depth->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    depth->setRange(0, 1, 1);

    EffectManifestParameterPointer width = pManifest->addParameter();
    width->setId("width");
    width->setName(QObject::tr("Width"));
    width->setShortName(QObject::tr("Width"));
    width->setDescription(QObject::tr(
            "Part of the period the audio stays audible\n"
            "10% - 90% of the effect period"));
    width->setValueScaler(EffectManifestParameter::ValueScaler::Linear);
    width->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    width->setRange(0.1, 0.5, 0.9);

    EffectManifestParameterPointer quantize = pManifest->addParameter();
    quantize->setId("quantize");
    quantize->setName(QObject::tr("Quantize"));
    quantize->setShortName(QObject::tr("Quantize"));
    quantize->setDescription(QObject::tr(
            "Round the Period parameter to the nearest whole division of a beat."));
    quantize->setValueScaler(EffectManifestParameter::ValueScaler::Toggle);
    quantize->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    quantize->setRange(0, 1, 1);

    EffectManifestParameterPointer triplet = pManifest->addParameter();
    triplet->setId("triplet");
    triplet->setName(QObject::tr("Triplets"));
    triplet->setShortName(QObject::tr("Triplet"));
    triplet->setDescription(QObject::tr(
            "When the Quantize parameter is enabled, divide the effect period by 3."));
    triplet->setValueScaler(EffectManifestParameter::ValueScaler::Toggle);
    triplet->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    triplet->setRange(0, 0, 1);

    return pManifest;
}

void TransEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pPeriodParameter = parameters.value("period");
    m_pDepthParameter = parameters.value("depth");
    m_pWidthParameter = parameters.value("width");
    m_pQuantizeParameter = parameters.value("quantize");
    m_pTripletParameter = parameters.value("triplet");
}

void TransEffect::processChannel(
        TransState* pState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& engineParameters,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    const double width = m_pWidthParameter->value();
    const double closedGain = 1.0 - m_pDepthParameter->value();
    const bool quantizeEnabled = m_pQuantizeParameter->toBool();
    const bool tripletEnabled = m_pTripletParameter->toBool();

    double period = m_pPeriodParameter->value();
    if (quantizeEnabled) {
        period = std::pow(2.0, std::round(std::log2(period)));
        if (tripletEnabled) {
            period /= 3.0;
        }
    }

    const bool hasBeats = groupFeatures.beat_length.has_value() &&
            groupFeatures.beat_length->seconds > 0.0 &&
            groupFeatures.beat_fraction_buffer_end.has_value();
    const double framesPerBeat = hasBeats
            ? groupFeatures.beat_length->seconds * engineParameters.sampleRate()
            : 0.0;

    // The period is a number of beats when a tempo is known, seconds otherwise
    const double framesPerPeriod = std::max(
            hasBeats ? period * framesPerBeat : period * engineParameters.sampleRate(),
            kMinFramesPerPeriod);

    double framesSincePeriodStart = pState->framesSincePeriodStart;
    if (enableState == EffectEnableState::Enabling ||
            quantizeEnabled != pState->quantizeEnabled ||
            tripletEnabled != pState->tripletEnabled) {
        // Start a period on the beat preceding this buffer
        framesSincePeriodStart = 0.0;
        if (hasBeats) {
            const double framesSinceBeat =
                    *groupFeatures.beat_fraction_buffer_end * framesPerBeat -
                    engineParameters.framesPerBuffer();
            framesSincePeriodStart = std::fmod(framesSinceBeat, framesPerBeat);
            if (framesSincePeriodStart < 0.0) {
                framesSincePeriodStart += framesPerBeat;
            }
        }
    }
    framesSincePeriodStart = std::fmod(framesSincePeriodStart, framesPerPeriod);

    const double openFrames = width * framesPerPeriod;
    const double fadeFrames = std::min(
            kFadeSeconds * engineParameters.sampleRate(), kMaxFadeRatio * framesPerPeriod);
    const double gainIncrement = 1.0 / fadeFrames;
    double gain = pState->gain;

    for (SINT i = 0;
            i < engineParameters.samplesPerBuffer();
            i += engineParameters.channelCount()) {
        const double gainTarget = framesSincePeriodStart < openFrames ? 1.0 : closedGain;
        gain = std::clamp(gainTarget, gain - gainIncrement, gain + gainIncrement);

        for (int channel = 0; channel < engineParameters.channelCount(); channel++) {
            pOutput[i + channel] = static_cast<CSAMPLE_GAIN>(gain) * pInput[i + channel];
        }

        framesSincePeriodStart += 1.0;
        if (framesSincePeriodStart >= framesPerPeriod) {
            framesSincePeriodStart -= framesPerPeriod;
        }
    }

    // Write back channel state
    pState->gain = gain;
    pState->framesSincePeriodStart = framesSincePeriodStart;
    pState->quantizeEnabled = quantizeEnabled;
    pState->tripletEnabled = tripletEnabled;
}
