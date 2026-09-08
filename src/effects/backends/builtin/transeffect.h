#pragma once

#include "effects/backends/effectmanifest.h"
#include "effects/backends/effectprocessor.h"
#include "engine/effects/engineeffectparameter.h"
#include "util/class.h"
#include "util/types.h"

class TransState : public EffectState {
  public:
    TransState(const mixxx::EngineParameters& engineParameters)
            : EffectState(engineParameters),
              gain(1.0),
              framesSincePeriodStart(0.0),
              quantizeEnabled(true),
              tripletEnabled(false){};
    ~TransState() override = default;

    double gain;
    double framesSincePeriodStart;
    bool quantizeEnabled;
    bool tripletEnabled;
};

class TransEffect : public EffectProcessorImpl<TransState> {
  public:
    TransEffect() = default;
    ~TransEffect() override = default;

    static QString getId();
    static EffectManifestPointer getManifest();

    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;

    void processChannel(
            TransState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

  private:
    QString debugString() const {
        return getId();
    }

    EngineEffectParameterPointer m_pPeriodParameter;
    EngineEffectParameterPointer m_pDepthParameter;
    EngineEffectParameterPointer m_pWidthParameter;
    EngineEffectParameterPointer m_pQuantizeParameter;
    EngineEffectParameterPointer m_pTripletParameter;

    DISALLOW_COPY_AND_ASSIGN(TransEffect);
};
