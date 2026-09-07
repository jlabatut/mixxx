#include "waveform/renderers/allshader/waveformrenderbeat.h"

#include <QDomNode>
#include <algorithm>

#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "waveform/renderers/allshader/matrixforwidgetgeometry.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

namespace {
// Height and half base width of the triangles marking the start of a measure,
// in renderer units.
constexpr float kMeasureMarkerSize = 8.f;

// Used for the measure markers unless the skin provides a MeasureColor.
constexpr QColor kDefaultMeasureColor = QColor(255, 60, 60);
} // namespace

namespace allshader {

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : WaveformRenderer(waveformWidget),
          m_measureColor(kDefaultMeasureColor),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
}

void WaveformRenderBeat::initializeGL() {
    WaveformRenderer::initializeGL();
    m_shader.init();
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_color = QColor(context.selectString(node, "BeatColor"));
    m_color = WSkinColor::getCorrectColor(m_color).toRgb();
    const QColor measureColor = context.selectColor(node, "MeasureColor");
    if (measureColor.isValid()) {
        m_measureColor = WSkinColor::getCorrectColor(measureColor).toRgb();
    }
}

void WaveformRenderBeat::paintGL() {
    TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_color.setAlphaF(alpha / 100.0f);

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    if (!startPosition.isValid() || !endPosition.isValid()) {
        return;
    }

    const float rendererBreadth = m_waveformRenderer->getBreadth();

    const int numVerticesPerLine = 6; // 2 triangles

    // Count the number of beats in the range to reserve space in the m_vertices vector.
    // Note that we could also use
    //   int numBearsInRange = trackBeats->numBeatsInRange(startPosition, endPosition);
    // for this, but there have been reports of that method failing with a DEBUG_ASSERT.
    const auto firstBeat = trackBeats->iteratorFrom(startPosition);

    int numBeatsInRange = 0;
    for (auto it = firstBeat;
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        numBeatsInRange++;
    }

    // Index of the first visible beat, counted from the first beat marker,
    // which is the reference downbeat of the beatgrid. Only valid, and only
    // computed, when there is a visible beat to count from.
    int beatIndex = numBeatsInRange > 0 ? firstBeat - trackBeats->cfirstmarker() : 0;

    const int reserved = numBeatsInRange * numVerticesPerLine;
    m_vertices.clear();
    m_vertices.reserve(reserved);
    m_measureVertices.clear();

    const float bottom = m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth;
    // Two triangles pointing towards the waveform, one at each edge.
    const float markerSize = std::min(kMeasureMarkerSize, bottom / 4.f);

    for (auto it = firstBeat;
            it != trackBeats->cend() && *it <= endPosition;
            ++it, ++beatIndex) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        beatPosition, positionType);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        const float x1 = static_cast<float>(xBeatPoint);
        const float x2 = x1 + 1.f;

        m_vertices.addRectangle(x1, 0.f, x2, bottom);

        if (m_waveformRenderer->isMeasureStart(beatIndex) && markerSize > 0.f) {
            const float x = x1 + 0.5f;
            m_measureVertices.addTriangle({x - markerSize, 0.f},
                    {x + markerSize, 0.f},
                    {x, markerSize});
            m_measureVertices.addTriangle({x - markerSize, bottom},
                    {x + markerSize, bottom},
                    {x, bottom - markerSize});
        }
    }

    DEBUG_ASSERT(reserved == m_vertices.size());

    const int positionLocation = m_shader.positionLocation();
    const int matrixLocation = m_shader.matrixLocation();
    const int colorLocation = m_shader.colorLocation();

    m_shader.bind();
    m_shader.enableAttributeArray(positionLocation);

    const QMatrix4x4 matrix = matrixForWidgetGeometry(m_waveformRenderer, false);

    m_shader.setAttributeArray(
            positionLocation, GL_FLOAT, m_vertices.constData(), 2);

    m_shader.setUniformValue(matrixLocation, matrix);
    m_shader.setUniformValue(colorLocation, m_color);

    glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());

    if (m_measureVertices.size() > 0) {
        QColor measureColor = m_measureColor;
        measureColor.setAlphaF(m_color.alphaF());
        m_shader.setAttributeArray(
                positionLocation, GL_FLOAT, m_measureVertices.constData(), 2);
        m_shader.setUniformValue(colorLocation, measureColor);
        glDrawArrays(GL_TRIANGLES, 0, m_measureVertices.size());
    }

    m_shader.disableAttributeArray(positionLocation);
    m_shader.release();
}

} // namespace allshader
