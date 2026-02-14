#include "VisualizerWidget.h"

#include "ProjectMEngine.h"

#include <QOpenGLContext>
#include <QPainter>
#include <QRect>
#include <QTimer>

VisualizerWidget::VisualizerWidget(ProjectMEngine *engine, QWindow *parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent), m_engine(engine) {
  setMinimumSize(QSize(420, 240));

  m_refreshTimer = new QTimer(this);
  m_refreshTimer->setInterval(16);
  connect(m_refreshTimer, &QTimer::timeout, this, qOverload<>(&VisualizerWidget::update));
  m_refreshTimer->start();
  m_fpsTimer.start();
}

VisualizerWidget::~VisualizerWidget() { cleanupGlResources(); }

void VisualizerWidget::consumeFrame(const QVector<float> &monoFrame) { m_lastFrame = monoFrame; }

void VisualizerWidget::setFpsDisplayEnabled(bool enabled) { m_showFps = enabled; }

void VisualizerWidget::initializeGL() {
  m_glCleanupDone = false;
  initializeOpenGLFunctions();
  if (context() != nullptr) {
    connect(context(),
            &QOpenGLContext::aboutToBeDestroyed,
            this,
            &VisualizerWidget::cleanupGlResources,
            Qt::DirectConnection);
  }
  if (m_engine != nullptr) {
    const int pixelWidth = qMax(1, static_cast<int>(width() * devicePixelRatioF()));
    const int pixelHeight = qMax(1, static_cast<int>(height() * devicePixelRatioF()));
    m_engine->initializeRenderer(pixelWidth, pixelHeight);
  }
}

void VisualizerWidget::resizeGL(int w, int h) {
  const int pixelWidth = qMax(1, static_cast<int>(w * devicePixelRatioF()));
  const int pixelHeight = qMax(1, static_cast<int>(h * devicePixelRatioF()));
  if (m_engine != nullptr) {
    m_engine->resizeRenderer(pixelWidth, pixelHeight);
  }
}

void VisualizerWidget::resizeEvent(QResizeEvent *event) {
  QOpenGLWindow::resizeEvent(event);
  const int pixelWidth = qMax(1, static_cast<int>(width() * devicePixelRatioF()));
  const int pixelHeight = qMax(1, static_cast<int>(height() * devicePixelRatioF()));
  if (m_engine != nullptr) {
    m_engine->resizeRenderer(pixelWidth, pixelHeight);
  }
  update();
}

void VisualizerWidget::cleanupGlResources() {
  if (m_glCleanupDone) {
    return;
  }
  m_glCleanupDone = true;

  if (m_engine == nullptr) {
    return;
  }

  if (isValid()) {
    makeCurrent();
    m_engine->resetRenderer();
    doneCurrent();
  } else {
    m_engine->resetRenderer();
  }
}

void VisualizerWidget::paintGL() {
  const int pixelWidth = qMax(1, static_cast<int>(width() * devicePixelRatioF()));
  const int pixelHeight = qMax(1, static_cast<int>(height() * devicePixelRatioF()));

  glViewport(0, 0, pixelWidth, pixelHeight);
  glDisable(GL_BLEND);
  glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (m_engine != nullptr) {
    m_engine->resizeRenderer(pixelWidth, pixelHeight);
  }

  const bool renderedProjectM =
      m_engine != nullptr && m_engine->renderFrame(static_cast<uint32_t>(defaultFramebufferObject()));

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, false);

  if (!renderedProjectM) {
    painter.setPen(QPen(QColor(60, 170, 245), 2));
    painter.drawText(12, 22, QStringLiteral("Preview fallback (projectM backend unavailable in this build)"));

    if (m_lastFrame.isEmpty()) {
      painter.setPen(QColor(190, 190, 190));
      painter.drawText(12, 46, QStringLiteral("Waiting for audio frames..."));
    } else {
      const int bars = 64;
      const int h = height() - 60;
      const int w = width() - 24;
      const int barW = qMax(2, w / bars);
      const int centerY = 40 + h / 2;

      painter.setBrush(QColor(65, 180, 255));
      painter.setPen(Qt::NoPen);

      for (int i = 0; i < bars; ++i) {
        const int sampleIndex = (i * m_lastFrame.size()) / bars;
        const float value = qAbs(m_lastFrame.value(sampleIndex));
        const int amplitude = qMin(h / 2, static_cast<int>(value * h * 0.8f));
        const int x = 12 + i * barW;
        painter.drawRect(x, centerY - amplitude, barW - 1, amplitude * 2);
      }
    }
  }

  ++m_fpsFrameCount;
  const qint64 elapsedMs = m_fpsTimer.elapsed();
  if (elapsedMs >= 500) {
    m_fpsValue = static_cast<float>(m_fpsFrameCount) * 1000.0f / static_cast<float>(elapsedMs);
    m_fpsFrameCount = 0;
    m_fpsTimer.restart();
  }

  if (m_showFps) {
    const QRect drawRect(0, 0, width(), height());
    painter.setPen(QColor(235, 235, 235));
    painter.drawText(drawRect.adjusted(0, 8, -10, 0), Qt::AlignTop | Qt::AlignRight,
                     QStringLiteral("FPS: %1").arg(QString::number(m_fpsValue, 'f', 1)));
  }
}
