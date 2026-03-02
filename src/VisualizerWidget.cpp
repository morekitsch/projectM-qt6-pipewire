#include "VisualizerWidget.h"

#include "ProjectMEngine.h"

#include <QDebug>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QPainter>
#include <QRect>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
constexpr const char *kUpscaleVertexShader = R"(#version 330 core
out vec2 vUv;

void main() {
  vec2 pos;
  if (gl_VertexID == 0) {
    pos = vec2(-1.0, -1.0);
  } else if (gl_VertexID == 1) {
    pos = vec2(3.0, -1.0);
  } else {
    pos = vec2(-1.0, 3.0);
  }

  vUv = 0.5 * (pos + 1.0);
  gl_Position = vec4(pos, 0.0, 1.0);
}
)";

constexpr const char *kUpscaleFragmentShader = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSourceTex;
uniform vec2 uSourceInvSize;
uniform float uSharpness;

void main() {
  vec3 center = texture(uSourceTex, vUv).rgb;
  vec3 north = texture(uSourceTex, vUv + vec2(0.0, uSourceInvSize.y)).rgb;
  vec3 south = texture(uSourceTex, vUv - vec2(0.0, uSourceInvSize.y)).rgb;
  vec3 east = texture(uSourceTex, vUv + vec2(uSourceInvSize.x, 0.0)).rgb;
  vec3 west = texture(uSourceTex, vUv - vec2(uSourceInvSize.x, 0.0)).rgb;

  vec3 laplacian = (north + south + east + west) - (4.0 * center);
  vec3 sharpened = center - (uSharpness * laplacian);
  fragColor = vec4(clamp(sharpened, 0.0, 1.0), 1.0);
}
)";

GLuint compileShader(GLenum shaderType, const char *sourceText) {
  QOpenGLContext *ctx = QOpenGLContext::currentContext();
  if (ctx == nullptr || ctx->functions() == nullptr) {
    return 0;
  }
  QOpenGLFunctions *functions = ctx->functions();

  GLuint shader = functions->glCreateShader(shaderType);
  if (shader == 0) {
    return 0;
  }

  functions->glShaderSource(shader, 1, &sourceText, nullptr);
  functions->glCompileShader(shader);
  GLint compiled = GL_FALSE;
  functions->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_TRUE) {
    return shader;
  }

  GLint logLength = 0;
  functions->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
  if (logLength > 1) {
    QByteArray log(logLength, '\0');
    functions->glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    qWarning().noquote() << "[qt6mplayer] Upscale shader compile failed:" << log;
  }

  functions->glDeleteShader(shader);
  return 0;
}

QOpenGLFunctions_3_3_Core *core33Functions() {
  QOpenGLContext *ctx = QOpenGLContext::currentContext();
  return ctx != nullptr ? QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(ctx)
                        : nullptr;
}
} // namespace

VisualizerWidget::VisualizerWidget(ProjectMEngine *engine, QWindow *parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent), m_engine(engine) {
  setMinimumSize(QSize(240, 135));

  m_refreshTimer = new QTimer(this);
  m_refreshTimer->setInterval(16);
  connect(m_refreshTimer, &QTimer::timeout, this, qOverload<>(&VisualizerWidget::update));
  m_refreshTimer->start();
  m_fpsTimer.start();
}

VisualizerWidget::~VisualizerWidget() { cleanupGlResources(); }

void VisualizerWidget::consumeFrame(const QVector<float> &monoFrame) { m_lastFrame = monoFrame; }

void VisualizerWidget::setFpsDisplayEnabled(bool enabled) { m_showFps = enabled; }

void VisualizerWidget::setRenderScalePercent(int percent) {
  const int clamped = qBound(50, percent, 100);
  if (clamped == m_renderScalePercent) {
    return;
  }

  m_renderScalePercent = clamped;

  if (isValid()) {
    makeCurrent();
    if (m_engine != nullptr) {
      const QSize outputSize = outputPixelSize();
      const QSize renderSize = rendererPixelSizeForOutput(outputSize.width(), outputSize.height());
      m_engine->resizeRenderer(renderSize.width(), renderSize.height());
    }
    if (m_renderScalePercent >= 100) {
      releaseUpscaleTarget();
    }
    doneCurrent();
  }

  update();
}

void VisualizerWidget::setUpscaleSharpness(double amount) {
  const float clamped = std::clamp(static_cast<float>(amount), 0.0f, 1.0f);
  if (qFuzzyCompare(clamped + 1.0f, m_upscaleSharpness + 1.0f)) {
    return;
  }
  m_upscaleSharpness = clamped;
  update();
}

void VisualizerWidget::showPresetOverlay(const QString &presetPath) {
  QString displayName = QFileInfo(presetPath).completeBaseName();
  if (displayName.isEmpty()) {
    displayName = QFileInfo(presetPath).fileName();
  }
  if (displayName.isEmpty()) {
    return;
  }

  m_presetOverlayText = displayName;
  m_presetOverlayTimer.restart();
  update();
}

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
    const QSize outputSize = outputPixelSize();
    const QSize renderSize = rendererPixelSizeForOutput(outputSize.width(), outputSize.height());
    m_engine->initializeRenderer(renderSize.width(), renderSize.height());
  }
}

void VisualizerWidget::resizeGL(int w, int h) {
  const int pixelWidth = qMax(1, static_cast<int>(std::lround(static_cast<double>(w) * devicePixelRatioF())));
  const int pixelHeight = qMax(1, static_cast<int>(std::lround(static_cast<double>(h) * devicePixelRatioF())));
  const QSize renderSize = rendererPixelSizeForOutput(pixelWidth, pixelHeight);
  if (m_engine != nullptr) {
    m_engine->resizeRenderer(renderSize.width(), renderSize.height());
  }
  if (renderSize.width() == pixelWidth && renderSize.height() == pixelHeight) {
    releaseUpscaleTarget();
  }
}

void VisualizerWidget::resizeEvent(QResizeEvent *event) {
  QOpenGLWindow::resizeEvent(event);
  const QSize outputSize = outputPixelSize();
  const QSize renderSize = rendererPixelSizeForOutput(outputSize.width(), outputSize.height());
  if (m_engine != nullptr) {
    m_engine->resizeRenderer(renderSize.width(), renderSize.height());
  }
  update();
}

void VisualizerWidget::cleanupGlResources() {
  if (m_glCleanupDone) {
    return;
  }
  m_glCleanupDone = true;

  if (isValid()) {
    makeCurrent();
    releaseUpscaleTarget();
    releaseUpscaleProgram();
    if (m_engine != nullptr) {
      m_engine->resetRenderer();
    }
    doneCurrent();
  } else {
    m_upscaleProgram = 0;
    m_upscaleVao = 0;
    m_upscaleColorTexture = 0;
    m_upscaleTargetWidth = 0;
    m_upscaleTargetHeight = 0;
    if (m_engine != nullptr) {
      m_engine->resetRenderer();
    }
  }
}

void VisualizerWidget::paintGL() {
  const QSize outputSize = outputPixelSize();
  const QSize renderSize = rendererPixelSizeForOutput(outputSize.width(), outputSize.height());
  const bool useUpscale = renderSize != outputSize;
  bool renderedProjectM = false;

  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(defaultFramebufferObject()));
  glViewport(0, 0, outputSize.width(), outputSize.height());
  glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (m_engine != nullptr && useUpscale && ensureUpscaleProgram()) {
    ensureUpscaleTarget(renderSize.width(), renderSize.height());
    if (m_upscaleColorTexture != 0) {
      glViewport(0, 0, renderSize.width(), renderSize.height());
      m_engine->resizeRenderer(renderSize.width(), renderSize.height());
      renderedProjectM = m_engine->renderFrame(static_cast<uint32_t>(defaultFramebufferObject()));
      if (renderedProjectM) {
        glBindTexture(GL_TEXTURE_2D, m_upscaleColorTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, renderSize.width(), renderSize.height());
        glBindTexture(GL_TEXTURE_2D, 0);
        glViewport(0, 0, outputSize.width(), outputSize.height());
        glClearColor(0.04f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderedProjectM = drawUpscaledScene(outputSize.width(), outputSize.height());
      }
    }
  } else if (!useUpscale) {
    releaseUpscaleTarget();
  }

  if (!renderedProjectM) {
    glViewport(0, 0, outputSize.width(), outputSize.height());
    if (m_engine != nullptr) {
      m_engine->resizeRenderer(outputSize.width(), outputSize.height());
      renderedProjectM = m_engine->renderFrame(static_cast<uint32_t>(defaultFramebufferObject()));
    }
  }

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

  if (!m_presetOverlayText.isEmpty() && m_presetOverlayTimer.isValid() &&
      m_presetOverlayTimer.elapsed() < m_presetOverlayDurationMs) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QString text = QStringLiteral("Preset: %1").arg(m_presetOverlayText);
    const QRect textRect = painter.fontMetrics().boundingRect(text).adjusted(-10, -6, 10, 6);
    const QRect bubbleRect = textRect.translated(14, height() - textRect.height() - 18);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(10, 14, 20, 190));
    painter.drawRoundedRect(bubbleRect, 8, 8);

    painter.setPen(QColor(230, 240, 255));
    painter.drawText(bubbleRect, Qt::AlignCenter, text);
  } else if (!m_presetOverlayText.isEmpty() && m_presetOverlayTimer.isValid()) {
    m_presetOverlayText.clear();
  }
}

QSize VisualizerWidget::outputPixelSize() const {
  const int pixelWidth =
      qMax(1, static_cast<int>(std::lround(static_cast<double>(width()) * devicePixelRatioF())));
  const int pixelHeight =
      qMax(1, static_cast<int>(std::lround(static_cast<double>(height()) * devicePixelRatioF())));
  return QSize(pixelWidth, pixelHeight);
}

QSize VisualizerWidget::rendererPixelSizeForOutput(int outputWidth, int outputHeight) const {
  const int scale = qBound(50, m_renderScalePercent, 100);
  const int renderWidth =
      qMax(1, static_cast<int>(std::lround(static_cast<double>(outputWidth) * static_cast<double>(scale) / 100.0)));
  const int renderHeight =
      qMax(1, static_cast<int>(std::lround(static_cast<double>(outputHeight) * static_cast<double>(scale) / 100.0)));
  return QSize(renderWidth, renderHeight);
}

void VisualizerWidget::ensureUpscaleTarget(int width, int height) {
  if (width <= 0 || height <= 0) {
    releaseUpscaleTarget();
    return;
  }

  if (m_upscaleColorTexture != 0 && m_upscaleTargetWidth == width && m_upscaleTargetHeight == height) {
    return;
  }

  releaseUpscaleTarget();

  glGenTextures(1, &m_upscaleColorTexture);
  glBindTexture(GL_TEXTURE_2D, m_upscaleColorTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  m_upscaleTargetWidth = width;
  m_upscaleTargetHeight = height;
}

void VisualizerWidget::releaseUpscaleTarget() {
  if (m_upscaleColorTexture != 0) {
    glDeleteTextures(1, &m_upscaleColorTexture);
    m_upscaleColorTexture = 0;
  }
  m_upscaleTargetWidth = 0;
  m_upscaleTargetHeight = 0;
}

bool VisualizerWidget::ensureUpscaleProgram() {
  if (m_upscaleProgram != 0) {
    return true;
  }
  if (m_upscaleProgramFailed) {
    return false;
  }

  const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kUpscaleVertexShader);
  const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kUpscaleFragmentShader);
  if (vertexShader == 0 || fragmentShader == 0) {
    if (vertexShader != 0) {
      glDeleteShader(vertexShader);
    }
    if (fragmentShader != 0) {
      glDeleteShader(fragmentShader);
    }
    m_upscaleProgramFailed = true;
    return false;
  }

  GLuint program = glCreateProgram();
  if (program == 0) {
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    m_upscaleProgramFailed = true;
    return false;
  }

  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength > 1) {
      QByteArray log(logLength, '\0');
      glGetProgramInfoLog(program, logLength, nullptr, log.data());
      qWarning().noquote() << "[qt6mplayer] Upscale program link failed:" << log;
    }
    glDeleteProgram(program);
    m_upscaleProgramFailed = true;
    return false;
  }

  m_upscaleProgram = program;
  if (m_upscaleVao == 0) {
    if (QOpenGLFunctions_3_3_Core *core = core33Functions()) {
      core->initializeOpenGLFunctions();
      core->glGenVertexArrays(1, &m_upscaleVao);
    } else {
      qWarning() << "[qt6mplayer] Missing OpenGL 3.3 core functions for upscaler.";
      glDeleteProgram(m_upscaleProgram);
      m_upscaleProgram = 0;
      m_upscaleProgramFailed = true;
      return false;
    }
  }
  return true;
}

void VisualizerWidget::releaseUpscaleProgram() {
  if (m_upscaleVao != 0) {
    if (QOpenGLFunctions_3_3_Core *core = core33Functions()) {
      core->initializeOpenGLFunctions();
      core->glDeleteVertexArrays(1, &m_upscaleVao);
    }
    m_upscaleVao = 0;
  }
  if (m_upscaleProgram != 0) {
    glDeleteProgram(m_upscaleProgram);
    m_upscaleProgram = 0;
  }
  m_upscaleProgramFailed = false;
}

bool VisualizerWidget::drawUpscaledScene(int outputWidth, int outputHeight) {
  Q_UNUSED(outputWidth);
  Q_UNUSED(outputHeight);

  if (m_upscaleProgram == 0 || m_upscaleColorTexture == 0 || m_upscaleTargetWidth <= 0 ||
      m_upscaleTargetHeight <= 0) {
    return false;
  }

  glDisable(GL_DEPTH_TEST);
  glUseProgram(m_upscaleProgram);

  const GLint sourceTexLocation = glGetUniformLocation(m_upscaleProgram, "uSourceTex");
  const GLint invSizeLocation = glGetUniformLocation(m_upscaleProgram, "uSourceInvSize");
  const GLint sharpnessLocation = glGetUniformLocation(m_upscaleProgram, "uSharpness");

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_upscaleColorTexture);
  glUniform1i(sourceTexLocation, 0);
  glUniform2f(invSizeLocation, 1.0f / static_cast<float>(m_upscaleTargetWidth),
              1.0f / static_cast<float>(m_upscaleTargetHeight));
  glUniform1f(sharpnessLocation, m_upscaleSharpness);

  QOpenGLFunctions_3_3_Core *core = core33Functions();
  if (core == nullptr) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    return false;
  }
  core->initializeOpenGLFunctions();
  core->glBindVertexArray(m_upscaleVao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  core->glBindVertexArray(0);

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  glEnable(GL_DEPTH_TEST);
  return true;
}
