#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWindow>
#include <QElapsedTimer>
#include <QResizeEvent>
#include <QSize>
#include <QVector>

class ProjectMEngine;
class QTimer;

class VisualizerWidget : public QOpenGLWindow, protected QOpenGLFunctions {
  Q_OBJECT

public:
  explicit VisualizerWidget(ProjectMEngine *engine, QWindow *parent = nullptr);
  ~VisualizerWidget() override;

public Q_SLOTS:
  void consumeFrame(const QVector<float> &monoFrame);
  void setFpsDisplayEnabled(bool enabled);
  void setRenderScalePercent(int percent);
  void setUpscaleSharpness(double amount);

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void resizeEvent(QResizeEvent *event) override;
  void paintGL() override;

private:
  void cleanupGlResources();
  QSize outputPixelSize() const;
  QSize rendererPixelSizeForOutput(int outputWidth, int outputHeight) const;
  void ensureUpscaleTarget(int width, int height);
  void releaseUpscaleTarget();
  bool ensureUpscaleProgram();
  void releaseUpscaleProgram();
  bool drawUpscaledScene(int outputWidth, int outputHeight);

  ProjectMEngine *m_engine = nullptr;
  QVector<float> m_lastFrame;
  QTimer *m_refreshTimer = nullptr;
  bool m_glCleanupDone = false;
  bool m_showFps = false;
  QElapsedTimer m_fpsTimer;
  int m_fpsFrameCount = 0;
  float m_fpsValue = 0.0f;
  int m_renderScalePercent = 77;
  float m_upscaleSharpness = 0.2f;
  bool m_upscaleProgramFailed = false;
  unsigned int m_upscaleProgram = 0;
  unsigned int m_upscaleVao = 0;
  unsigned int m_upscaleColorTexture = 0;
  int m_upscaleTargetWidth = 0;
  int m_upscaleTargetHeight = 0;
};
