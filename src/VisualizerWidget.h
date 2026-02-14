#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWindow>
#include <QElapsedTimer>
#include <QResizeEvent>
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

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void resizeEvent(QResizeEvent *event) override;
  void paintGL() override;

private:
  void cleanupGlResources();

  ProjectMEngine *m_engine = nullptr;
  QVector<float> m_lastFrame;
  QTimer *m_refreshTimer = nullptr;
  bool m_glCleanupDone = false;
  bool m_showFps = false;
  QElapsedTimer m_fpsTimer;
  int m_fpsFrameCount = 0;
  float m_fpsValue = 0.0f;
};
