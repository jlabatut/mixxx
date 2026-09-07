#include "waveform/renderers/waveformrenderbeat.h"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>

#include "track/track.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

class QPaintEvent;

namespace {
// Height and half base width of the triangles marking the start of a measure.
constexpr double kMeasureMarkerSize = 8.0;

// Used for the measure markers unless the skin provides a MeasureColor.
constexpr QColor kDefaultMeasureColor = QColor(255, 60, 60);
} // namespace

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidgetRenderer)
        : WaveformRendererAbstract(waveformWidgetRenderer),
          m_measureColor(kDefaultMeasureColor) {
    m_beats.resize(128);
    m_measures.resize(32);
}

WaveformRenderBeat::~WaveformRenderBeat() {
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_beatColor = QColor(context.selectString(node, "BeatColor"));
    m_beatColor = WSkinColor::getCorrectColor(m_beatColor).toRgb();
    const QColor measureColor = context.selectColor(node, "MeasureColor");
    if (measureColor.isValid()) {
        m_measureColor = WSkinColor::getCorrectColor(measureColor).toRgb();
    }
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* /*event*/) {
    TrackPointer pTrackInfo = m_waveformRenderer->getTrackInfo();

    if (!pTrackInfo) {
        return;
    }

    mixxx::BeatsPointer trackBeats = pTrackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return;
    }
#ifdef MIXXX_USE_QOPENGL
    // Using alpha transparency with drawLines causes a graphical issue when
    // drawing with QPainter on the QOpenGLWindow: instead of individual lines
    // a large rectangle encompassing all beatlines is drawn.
    m_beatColor.setAlphaF(1.f);
#else
    m_beatColor.setAlphaF(alpha/100.0);
#endif

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition();
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition();

    // qDebug() << "trackSamples" << trackSamples
    //          << "firstDisplayedPosition" << firstDisplayedPosition
    //          << "lastDisplayedPosition" << lastDisplayedPosition;

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);
    auto it = trackBeats->iteratorFrom(startPosition);

    // if no beat do not waste time saving/restoring painter
    if (it == trackBeats->cend() || *it > endPosition) {
        return;
    }

    PainterScope PainterScope(painter);

    painter->setRenderHint(QPainter::Antialiasing);

    const double beatLineWidth = std::max(1.0, scaleFactor());

    const Qt::Orientation orientation = m_waveformRenderer->getOrientation();
    const float rendererWidth = m_waveformRenderer->getWidth();
    const float rendererHeight = m_waveformRenderer->getHeight();

    // Index of the first visible beat, counted from the first beat marker,
    // which is the reference downbeat of the beatgrid.
    int beatIndex = it - trackBeats->cfirstmarker();

    int beatCount = 0;
    int measureCount = 0;

    for (; it != trackBeats->cend() && *it <= endPosition; ++it, ++beatIndex) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(beatPosition);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        // If we don't have enough space, double the size.
        if (beatCount >= m_beats.size()) {
            m_beats.resize(m_beats.size() * 2);
        }

        if (orientation == Qt::Horizontal) {
            m_beats[beatCount].setLine(xBeatPoint, 0.0f, xBeatPoint, rendererHeight);
        } else {
            m_beats[beatCount].setLine(0.0f, xBeatPoint, rendererWidth, xBeatPoint);
        }

        // Measure starts get the same line as any other beat, plus a marker.
        if (m_waveformRenderer->isMeasureStart(beatIndex)) {
            if (measureCount >= m_measures.size()) {
                m_measures.resize(m_measures.size() * 2);
            }
            m_measures[measureCount++] = m_beats[beatCount];
        }
        beatCount++;
    }

    QPen beatPen(m_beatColor);
    beatPen.setWidthF(beatLineWidth);
    painter->setPen(beatPen);
    // Make sure to use constData to prevent detaches!
    painter->drawLines(m_beats.constData(), beatCount);

    if (measureCount > 0) {
        QColor measureColor = m_measureColor;
        measureColor.setAlphaF(m_beatColor.alphaF());

        // Two triangles pointing towards the waveform, one at each end of the
        // measure line. All measure lines are parallel and span the whole
        // breadth, so the triangles all have the same shape.
        const double breadth = orientation == Qt::Horizontal ? rendererHeight : rendererWidth;
        const double size = std::min(kMeasureMarkerSize, breadth / 4.0);
        if (size > 0.0) {
            const QPointF along = orientation == Qt::Horizontal
                    ? QPointF(0.0, size)
                    : QPointF(size, 0.0);
            const QPointF across(-along.y(), along.x());

            painter->setPen(Qt::NoPen);
            painter->setBrush(measureColor);
            QPolygonF marker;
            marker.resize(3);
            for (int i = 0; i < measureCount; i++) {
                const QLineF& line = m_measures.at(i);

                marker[0] = line.p1() - across;
                marker[1] = line.p1() + across;
                marker[2] = line.p1() + along;
                painter->drawPolygon(marker);

                marker[0] = line.p2() - across;
                marker[1] = line.p2() + across;
                marker[2] = line.p2() - along;
                painter->drawPolygon(marker);
            }
        }
    }
}
