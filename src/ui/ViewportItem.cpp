#include "ViewportItem.hpp"

#include "DocumentController.hpp"
#include "timberlab/core/align.hpp"

#include <QByteArray>
#include <QDebug>
#include <QLineF>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QQuickWindow>

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace timberlab::ui {
namespace {

constexpr float kFov = 40.f;

// Bodies are shared. The version line is chosen when the context is created:
// #version 330 core on desktop GL, #version 300 es on OpenGL ES 3.0.
const char* kMeshVertexBody = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vNormal;
void main() {
  vNormal = mat3(uModel) * aNormal;
  gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kMeshFragmentBody = R"(
in vec3 vNormal;
out vec4 fragColor;
uniform vec3 uColor;
uniform float uSelected;
void main() {
  vec3 normal = normalize(vNormal);
  vec3 light = normalize(vec3(0.35, -0.45, 0.82));
  float wrap = dot(normal, light) * 0.5 + 0.5;
  vec3 lit = uColor * (0.30 + 0.70 * wrap);
  vec3 amber = vec3(0.878, 0.627, 0.353);
  lit = mix(lit, amber, 0.62 * uSelected);
  fragColor = vec4(lit, 1.0);
}
)";

const char* kPickVertexBody = R"(
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(aPos, 1.0); }
)";

const char* kPickFragmentBody = R"(
layout(location = 0) out int fragId;
uniform int uId;
void main() { fragId = uId; }
)";

const char* kLineVertexBody = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uViewProj;
out vec3 vColor;
void main() {
  vColor = aColor;
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* kLineFragmentBody = R"(
in vec3 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(vColor, 1.0); }
)";

QByteArray shaderSource(bool es, bool fragment, const char* body) {
  QByteArray source = es ? QByteArray("#version 300 es\n") : QByteArray("#version 330 core\n");
  if (es && fragment) source += "precision highp float;\nprecision highp int;\n";
  source += body;
  return source;
}

bool compileShader(QOpenGLShaderProgram& program, QOpenGLShader::ShaderType type, const QByteArray& source) {
  if (program.addShaderFromSourceCode(type, source)) return true;
  qWarning("TimberLab shader compile failed: %s", qPrintable(program.log()));
  return false;
}

bool linkProgram(QOpenGLShaderProgram& program, bool es, const char* vertex, const char* fragment) {
  if (!compileShader(program, QOpenGLShader::Vertex, shaderSource(es, false, vertex))) return false;
  if (!compileShader(program, QOpenGLShader::Fragment, shaderSource(es, true, fragment))) return false;
  if (program.link()) return true;
  qWarning("TimberLab shader link failed: %s", qPrintable(program.log()));
  return false;
}

struct DrawPart {
  QString id;
  QMatrix4x4 model;
  QVector3D color;
  int indexCount = 0;
  QOpenGLVertexArrayObject vao;
  QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
};

class ViewportRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLExtraFunctions {
 public:
  ViewportRenderer() { initializeOpenGLFunctions(); }
  ~ViewportRenderer() override { destroyPickTarget(); }

  void synchronize(QQuickFramebufferObject* item) override {
    auto* view = static_cast<ViewportItem*>(item);
    controller_ = view;
    const float aspect = view->height() > 0 ? float(view->width() / view->height()) : 1.f;
    const QVector3D eye = view->eye();
    const QVector3D up = std::abs(view->elevation()) > 80.f ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
    view_.setToIdentity();
    view_.lookAt(eye, view->target(), up);
    const float nearPlane = std::max(0.5f, view->distance() * 0.001f);
    const float farPlane = std::max(nearPlane + 10.f, view->distance() * 40.f);
    projection_.setToIdentity();
    if (view->orthographic()) {
      const float halfHeight = view->distance() * 0.45f;
      const float halfWidth = halfHeight * aspect;
      projection_.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
    } else {
      projection_.perspective(kFov, aspect, nearPlane, farPlane);
    }
    selectedId_ = view->selectedId();
    if (view->controller() != nullptr) {
      parts_ = view->controller()->copyScene();
      const auto* controller = view->controller();
      if (controller->isDragging()) {
        guideCenter_ = controller->dragCenter();
        guideSnapX_ = controller->dragSnapX();
        guideSnapY_ = controller->dragSnapY();
        guideSnapZ_ = controller->dragSnapZ();
      } else {
        guideCount_ = 0;
      }
    } else {
      guideCount_ = 0;
    }
    geometryDirty_ = true;
    guideDirty_ = true;
    dpr_ = view->window() != nullptr ? float(view->window()->devicePixelRatio()) : 1.f;
    if (view->pickPending() && view->pickSerial() != consumedPick_) {
      pick_ = true;
      consumedPick_ = view->pickSerial();
      pickX_ = int(view->pickPoint().x() * dpr_);
      pickY_ = int(view->pickPoint().y() * dpr_);
      view->clearPick();
    }
  }

  void render() override {
    ensurePrograms();
    ensureGrid();
    ensureGuides();
    if (geometryDirty_) uploadParts();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.176f, 0.169f, 0.161f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawGrid();
    drawGuides();
    drawMeshes(false);

    if (pick_) {
      pick_ = false;
      const QString picked = readPickId();
      if (controller_ != nullptr) {
        QMetaObject::invokeMethod(controller_, "deliverPick", Qt::QueuedConnection, Q_ARG(QString, picked));
      }
      framebufferObject()->bind();
    }
  }

 private:
  void ensurePrograms() {
    if (shaderAttempted_) return;
    const QOpenGLContext* context = QOpenGLContext::currentContext();
    if (context == nullptr) return;
    shaderAttempted_ = true;
    const bool es = context->isOpenGLES();
    linkProgram(meshProgram_, es, kMeshVertexBody, kMeshFragmentBody);
    linkProgram(pickProgram_, es, kPickVertexBody, kPickFragmentBody);
    linkProgram(lineProgram_, es, kLineVertexBody, kLineFragmentBody);
  }

  void ensureGrid() {
    if (gridReady_) return;
    std::vector<float> lines;
    auto push = [&](float x, float y, float z, float r, float g, float b) {
      lines.insert(lines.end(), {x, y, z, r, g, b});
    };
    const float minor[3] = {0.29f, 0.27f, 0.25f};
    const float major[3] = {0.42f, 0.38f, 0.33f};
    for (int i = -20; i <= 20; ++i) {
      const float at = float(i) * 100.f;
      const float* color = (i % 5 == 0) ? major : minor;
      push(-2000, at, 0, color[0], color[1], color[2]);
      push(2000, at, 0, color[0], color[1], color[2]);
      push(at, -2000, 0, color[0], color[1], color[2]);
      push(at, 2000, 0, color[0], color[1], color[2]);
    }
    gridCount_ = int(lines.size() / 6);
    gridVao_.create();
    gridVao_.bind();
    gridVbo_.create();
    gridVbo_.bind();
    gridVbo_.allocate(lines.data(), int(lines.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    gridVao_.release();
    gridReady_ = true;
  }

  void uploadParts() {
    gpu_.clear();
    pickIds_.clear();
    pickIds_.push_back(QString());
    for (const ScenePart& part : parts_) {
      if (!part.visible || part.indices.empty()) continue;
      auto draw = std::make_unique<DrawPart>();
      draw->id = part.id;
      draw->model = part.model;
      draw->color = part.color;
      draw->indexCount = int(part.indices.size());
      draw->vao.create();
      draw->vao.bind();
      draw->vbo.create();
      draw->vbo.bind();
      draw->vbo.allocate(part.interleaved.data(), int(part.interleaved.size() * sizeof(float)));
      draw->ibo.create();
      draw->ibo.bind();
      draw->ibo.allocate(part.indices.data(), int(part.indices.size() * sizeof(std::uint32_t)));
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
      draw->vao.release();
      pickIds_.push_back(part.id);
      gpu_.push_back(std::move(draw));
    }
    geometryDirty_ = false;
  }

  void drawGrid() {
    if (!lineProgram_.isLinked()) return;
    lineProgram_.bind();
    lineProgram_.setUniformValue("uViewProj", projection_ * view_);
    gridVao_.bind();
    glDrawArrays(GL_LINES, 0, gridCount_);
    gridVao_.release();
    lineProgram_.release();
  }

  void ensureGuides() {
    if (!guideDirty_) return;
    uploadGuides();
  }

  void uploadGuides() {
    guideCount_ = 0;
    if (guideCenter_.isNull()) {
      guideDirty_ = false;
      return;
    }
    std::vector<float> lines;
    auto push = [&](float x, float y, float z, float r, float g, float b) {
      lines.insert(lines.end(), {x, y, z, r, g, b});
    };
    const float span = 5000.f;
    const float dim = 0.25f;
    const QVector3D c = guideCenter_;

    const float xr = guideSnapX_ ? 0.95f : dim;
    const float xg = guideSnapX_ ? 0.70f : dim;
    const float xb = guideSnapX_ ? 0.25f : dim;
    push(c.x() - span, c.y(), c.z(), xr, xg, xb);
    push(c.x() + span, c.y(), c.z(), xr, xg, xb);

    const float yr = guideSnapY_ ? 0.95f : dim;
    const float yg = guideSnapY_ ? 0.70f : dim;
    const float yb = guideSnapY_ ? 0.25f : dim;
    push(c.x(), c.y() - span, c.z(), yr, yg, yb);
    push(c.x(), c.y() + span, c.z(), yr, yg, yb);

    const float zr = guideSnapZ_ ? 0.95f : dim;
    const float zg = guideSnapZ_ ? 0.70f : dim;
    const float zb = guideSnapZ_ ? 0.25f : dim;
    push(c.x(), c.y(), c.z() - span, zr, zg, zb);
    push(c.x(), c.y(), c.z() + span, zr, zg, zb);

    guideCount_ = int(lines.size() / 6);
    if (!guideVao_.isCreated()) guideVao_.create();
    guideVao_.bind();
    if (!guideVbo_.isCreated()) guideVbo_.create();
    guideVbo_.bind();
    guideVbo_.allocate(lines.data(), int(lines.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    guideVao_.release();
    guideDirty_ = false;
  }

  void drawGuides() {
    if (!lineProgram_.isLinked() || guideCount_ == 0) return;
    lineProgram_.bind();
    lineProgram_.setUniformValue("uViewProj", projection_ * view_);
    guideVao_.bind();
    glDrawArrays(GL_LINES, 0, guideCount_);
    guideVao_.release();
    lineProgram_.release();
  }

  void drawMeshes(bool picking) {
    QOpenGLShaderProgram& program = picking ? pickProgram_ : meshProgram_;
    if (!program.isLinked()) return;
    program.bind();
    int id = 1;
    for (const auto& part : gpu_) {
      const QMatrix4x4 mvp = projection_ * view_ * part->model;
      program.setUniformValue("uMvp", mvp);
      if (picking) {
        program.setUniformValue("uId", id);
      } else {
        program.setUniformValue("uModel", part->model);
        program.setUniformValue("uColor", part->color);
        program.setUniformValue("uSelected", part->id == selectedId_ ? 1.f : 0.f);
      }
      part->vao.bind();
      glDrawElements(GL_TRIANGLES, part->indexCount, GL_UNSIGNED_INT, nullptr);
      part->vao.release();
      ++id;
    }
    program.release();
  }

  void ensurePickTarget(int width, int height) {
    if (pickWidth_ == width && pickHeight_ == height && pickFbo_ != 0) return;
    destroyPickTarget();
    glGenFramebuffers(1, &pickFbo_);
    glGenTextures(1, &pickColor_);
    glBindTexture(GL_TEXTURE_2D, pickColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, width, height, 0, GL_RED_INTEGER, GL_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &pickDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, pickDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pickColor_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pickDepth_);
    pickWidth_ = width;
    pickHeight_ = height;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void destroyPickTarget() {
    if (pickFbo_ != 0) glDeleteFramebuffers(1, &pickFbo_);
    if (pickColor_ != 0) glDeleteTextures(1, &pickColor_);
    if (pickDepth_ != 0) glDeleteRenderbuffers(1, &pickDepth_);
    pickFbo_ = pickColor_ = pickDepth_ = 0;
  }

  QString readPickId() {
    const QSize size = framebufferObject()->size();
    if (size.width() < 1 || size.height() < 1) return {};
    ensurePickTarget(size.width(), size.height());
    glBindFramebuffer(GL_FRAMEBUFFER, pickFbo_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      framebufferObject()->bind();
      return {};
    }
    glViewport(0, 0, size.width(), size.height());
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    const GLint clearId = 0;
    glClearBufferiv(GL_COLOR, 0, &clearId);
    glClear(GL_DEPTH_BUFFER_BIT);
    drawMeshes(true);
    const int x = std::clamp(pickX_, 0, size.width() - 1);
    const int y = std::clamp(size.height() - 1 - pickY_, 0, size.height() - 1);
    GLint pixel = 0;
    glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixel);
    framebufferObject()->bind();
    glViewport(0, 0, size.width(), size.height());
    if (pixel <= 0 || pixel >= int(pickIds_.size())) return {};
    pickedId_ = pickIds_[static_cast<std::size_t>(pixel)];
    return pickedId_;
  }

  QQuickFramebufferObject* controller_ = nullptr;
  QMatrix4x4 view_;
  QMatrix4x4 projection_;
  QString selectedId_;
  std::vector<ScenePart> parts_;
  std::vector<std::unique_ptr<DrawPart>> gpu_;
  std::vector<QString> pickIds_;
  QString pickedId_;
  bool geometryDirty_ = false;
  bool pick_ = false;
  int consumedPick_ = 0;
  int pickX_ = 0;
  int pickY_ = 0;
  float dpr_ = 1.f;
  QOpenGLShaderProgram meshProgram_;
  QOpenGLShaderProgram pickProgram_;
  QOpenGLShaderProgram lineProgram_;
  bool shaderAttempted_ = false;
  QOpenGLVertexArrayObject gridVao_;
  QOpenGLBuffer gridVbo_{QOpenGLBuffer::VertexBuffer};
  int gridCount_ = 0;
  bool gridReady_ = false;
  QOpenGLVertexArrayObject guideVao_;
  QOpenGLBuffer guideVbo_{QOpenGLBuffer::VertexBuffer};
  int guideCount_ = 0;
  bool guideDirty_ = false;
  QVector3D guideCenter_;
  bool guideSnapX_ = false;
  bool guideSnapY_ = false;
  bool guideSnapZ_ = false;
  GLuint pickFbo_ = 0;
  GLuint pickColor_ = 0;
  GLuint pickDepth_ = 0;
  int pickWidth_ = 0;
  int pickHeight_ = 0;
};

bool isRealMouse(const QMouseEvent* event) {
  return event->source() == Qt::MouseEventNotSynthesized;
}

}  // namespace

ViewportItem::ViewportItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {
  setAcceptedMouseButtons(Qt::AllButtons);
  setAcceptTouchEvents(true);
  setMirrorVertically(true);
  setFlag(ItemIsFocusScope, true);
}

QQuickFramebufferObject::Renderer* ViewportItem::createRenderer() const { return new ViewportRenderer; }

void ViewportItem::setController(DocumentController* controller) {
  if (controller_ == controller) return;
  controller_ = controller;
  emit controllerChanged();
  update();
}

QString ViewportItem::selectedId() const {
  return controller_ != nullptr ? controller_->selectedId() : QString();
}

QVector3D ViewportItem::eye() const {
  const float azimuth = qDegreesToRadians(azimuth_);
  const float elevation = qDegreesToRadians(elevation_);
  const float x = distance_ * std::cos(elevation) * std::sin(azimuth);
  const float y = -distance_ * std::cos(elevation) * std::cos(azimuth);
  const float z = distance_ * std::sin(elevation);
  return target_ + QVector3D(x, y, z);
}

void ViewportItem::orbit(QPointF delta) {
  azimuth_ += float(delta.x()) * 0.35f;
  elevation_ = std::clamp(elevation_ + float(delta.y()) * 0.28f, -85.f, 85.f);
  orthographic_ = false;
  update();
}

void ViewportItem::pan(QPointF delta) {
  const QVector3D eyePos = eye();
  QVector3D forward = (target_ - eyePos);
  if (forward.lengthSquared() < 1e-6f) return;
  forward.normalize();
  const QVector3D worldUp = std::abs(elevation_) > 80.f ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
  QVector3D right = QVector3D::crossProduct(forward, worldUp);
  if (right.lengthSquared() < 1e-8f) right = QVector3D(1, 0, 0);
  right.normalize();
  const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
  const float scale = distance_ * 0.0014f;
  target_ += right * float(-delta.x()) * scale + up * float(delta.y()) * scale;
  update();
}

void ViewportItem::zoomBy(float factor) {
  distance_ = std::clamp(distance_ * factor, 30.f, 80000.f);
  update();
}

void ViewportItem::requestPick(QPointF position) {
  pickPoint_ = position;
  pickPending_ = true;
  ++pickSerial_;
  update();
}

void ViewportItem::fit(bool selectionOnly) {
  if (controller_ == nullptr) return;
  QVector3D minimum;
  QVector3D maximum;
  const QString id = selectionOnly ? controller_->selectedId() : QString();
  if (!controller_->worldBounds(id, minimum, maximum)) {
    target_ = QVector3D(0, 0, 100);
    distance_ = 1600.f;
    update();
    return;
  }
  target_ = (minimum + maximum) * 0.5f;
  const float radius = std::max(50.f, (maximum - minimum).length() * 0.5f);
  distance_ = std::clamp(radius / std::tan(qDegreesToRadians(kFov * 0.5f)) * 1.15f, 80.f, 80000.f);
  update();
}

void ViewportItem::frameAll() { fit(false); }

void ViewportItem::frameSelection() { fit(true); }

void ViewportItem::viewProjection(QMatrix4x4& view, QMatrix4x4& projection) const {
  const QVector3D up = std::abs(elevation_) > 80.f ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
  view.setToIdentity();
  view.lookAt(eye(), target_, up);
  const float aspect = height() > 0 ? float(width()) / float(height()) : 1.f;
  const float nearPlane = std::max(0.5f, distance_ * 0.001f);
  const float farPlane = std::max(nearPlane + 10.f, distance_ * 40.f);
  projection.setToIdentity();
  if (orthographic_) {
    const float halfHeight = distance_ * 0.45f;
    const float halfWidth = halfHeight * aspect;
    projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
  } else {
    projection.perspective(kFov, aspect, nearPlane, farPlane);
  }
}

bool ViewportItem::rayThrough(QPointF position, QVector3D& origin, QVector3D& direction) const {
  if (width() <= 0 || height() <= 0) return false;
  QMatrix4x4 view, projection;
  viewProjection(view, projection);
  const float x = 2.f * float(position.x()) / float(width()) - 1.f;
  const float y = 1.f - 2.f * float(position.y()) / float(height());
  const QMatrix4x4 vp = projection * view;
  const QMatrix4x4 inv = vp.inverted();
  if (orthographic_) {
    const QVector4D p = inv * QVector4D(x, y, 0.f, 1.f);
    origin = QVector3D(p.x() / p.w(), p.y() / p.w(), p.z() / p.w());
    direction = (target_ - eye()).normalized();
    if (direction.lengthSquared() < 1e-12f) direction = QVector3D(0, 0, -1);
  } else {
    origin = eye();
    const QVector4D far = inv * QVector4D(x, y, 1.f, 1.f);
    const QVector3D farWorld(far.x() / far.w(), far.y() / far.w(), far.z() / far.w());
    direction = (farWorld - origin).normalized();
    if (direction.lengthSquared() < 1e-12f) return false;
  }
  return true;
}

bool ViewportItem::projectToScreen(const QVector3D& world, QPointF& screen) const {
  if (width() <= 0 || height() <= 0) return false;
  QMatrix4x4 view, projection;
  viewProjection(view, projection);
  const QVector3D clip = (projection * view).map(world);
  if (clip.z() < -1.f || clip.z() > 1.f) return false;
  screen.rx() = (clip.x() + 1.f) * 0.5f * width();
  screen.ry() = (1.f - clip.y()) * 0.5f * height();
  return true;
}

bool ViewportItem::planeHit(QPointF position, QVector3D& hit) const {
  QVector3D origin, direction;
  if (!rayThrough(position, origin, direction)) return false;
  const float denom = QVector3D::dotProduct(direction, planeNormal_);
  if (std::abs(denom) < 1e-6f) return false;
  const float t = QVector3D::dotProduct(planePoint_ - origin, planeNormal_) / denom;
  if (t < 0.f) return false;
  hit = origin + t * direction;
  return true;
}

bool ViewportItem::screenBox(const ContactZone& zone, QRectF& rect) const {
  const QVector3D corners[8] = {
      zone.minimum,
      QVector3D(zone.maximum.x(), zone.minimum.y(), zone.minimum.z()),
      QVector3D(zone.minimum.x(), zone.maximum.y(), zone.minimum.z()),
      QVector3D(zone.minimum.x(), zone.minimum.y(), zone.maximum.z()),
      QVector3D(zone.maximum.x(), zone.maximum.y(), zone.minimum.z()),
      QVector3D(zone.maximum.x(), zone.minimum.y(), zone.maximum.z()),
      QVector3D(zone.minimum.x(), zone.maximum.y(), zone.maximum.z()),
      zone.maximum};
  bool any = false;
  QPointF minScreen, maxScreen;
  for (const QVector3D& corner : corners) {
    QPointF screen;
    if (!projectToScreen(corner, screen)) continue;
    if (!any) {
      minScreen = maxScreen = screen;
      any = true;
      continue;
    }
    minScreen.rx() = std::min(minScreen.x(), screen.x());
    minScreen.ry() = std::min(minScreen.y(), screen.y());
    maxScreen.rx() = std::max(maxScreen.x(), screen.x());
    maxScreen.ry() = std::max(maxScreen.y(), screen.y());
  }
  if (!any) return false;
  rect = QRectF(minScreen, maxScreen);
  return true;
}

bool ViewportItem::contactUnder(QPointF position, QString& idA, QString& idB) const {
  if (controller_ == nullptr) return false;
  QVector3D origin, direction;
  if (!rayThrough(position, origin, direction)) return false;
  const double o[3] = {origin.x(), origin.y(), origin.z()};
  const double d[3] = {direction.x(), direction.y(), direction.z()};
  const std::vector<ContactZone> zones = controller_->contactZones();
  double best = 1e300;
  for (const ContactZone& zone : zones) {
    timberlab::WorldBox box;
    box.minX = zone.minimum.x();
    box.minY = zone.minimum.y();
    box.minZ = zone.minimum.z();
    box.maxX = zone.maximum.x();
    box.maxY = zone.maximum.y();
    box.maxZ = zone.maximum.z();
    double tEnter = 0;
    if (!timberlab::rayHitsBox(o, d, box, tEnter)) continue;
    if (tEnter >= best) continue;
    best = tEnter;
    idA = zone.idA;
    idB = zone.idB;
  }
  return best < 1e300;
}

void ViewportItem::beginLeftPress(QPointF position, bool shift) {
  pressKind_ = PressKind::Orbit;
  moveStarted_ = false;
  blockNoted_ = false;
  pressPartId_.clear();
  if (shift || controller_ == nullptr) return;
  QVector3D origin, direction;
  if (!rayThrough(position, origin, direction)) return;
  const QString id = controller_->pickPart(origin, direction);
  const auto start = controller_->classifyDrag(id);
  if (start == DocumentController::DragStart::Blocked) {
    pressKind_ = PressKind::Blocked;
    pressPartId_ = id;
    controller_->reportDragBlocked();
    return;
  }
  if (start != DocumentController::DragStart::Move || !controller_->beginPartDrag(id)) return;
  pressKind_ = PressKind::Move;
  pressPartId_ = id;
  grabPoint_ = controller_->dragAnchor();
  planeNormal_ = (target_ - eye()).normalized();
  if (planeNormal_.lengthSquared() < 1e-12f) planeNormal_ = QVector3D(0, 0, 1);
  planePoint_ = grabPoint_;
}

void ViewportItem::movePressedPart(QPointF position) {
  if (pressKind_ == PressKind::Orbit) {
    orbit(position - lastPos_);
    lastPos_ = position;
    return;
  }
  if (pressKind_ != PressKind::Move) return;
  QVector3D hit;
  if (!planeHit(position, hit)) return;
  if (!moveStarted_) {
    moveStarted_ = true;
    grabPoint_ = hit;
  }
  const QVector3D delta = hit - grabPoint_;
  const auto preview = controller_->previewPartDrag(delta.x(), delta.y(), delta.z());
  if (preview.ok) {
    const QVector3D pos = controller_->dragPositionMm();
    const qreal nextX = pos.x();
    const qreal nextY = pos.y();
    const qreal nextZ = pos.z();
    const QPointF nextPos = position + QPointF(14, -30);
    if (!dragOverlayVisible_ || dragOverlayX_ != nextX || dragOverlayY_ != nextY || dragOverlayZ_ != nextZ ||
        dragSnapX_ != preview.snapX || dragSnapY_ != preview.snapY || dragSnapZ_ != preview.snapZ ||
        dragOverlayPosition_ != nextPos) {
      dragOverlayVisible_ = true;
      dragOverlayX_ = nextX;
      dragOverlayY_ = nextY;
      dragOverlayZ_ = nextZ;
      dragSnapX_ = preview.snapX;
      dragSnapY_ = preview.snapY;
      dragSnapZ_ = preview.snapZ;
      dragOverlayPosition_ = nextPos;
      emit dragOverlayChanged();
    }
  } else if (dragOverlayVisible_) {
    dragOverlayVisible_ = false;
    emit dragOverlayChanged();
  }
  update();
  lastPos_ = position;
}

void ViewportItem::showContactMenu(QPointF position) {
  if (controller_ == nullptr) return;
  QString idA, idB;
  if (!contactUnder(position, idA, idB)) return;
  if (!controller_->armContact(idA, idB)) return;
  emit contactMenuRequested(position.x(), position.y());
}

void ViewportItem::finishPress(bool commitMove) {
  if (pressKind_ == PressKind::Move) {
    if (commitMove && moveStarted_)
      controller_->commitPartDrag();
    else
      controller_->cancelPartDrag();
  } else if (pressKind_ == PressKind::Orbit && !moveStarted_ && !dragging_) {
    requestPick(pressPos_);
  }
  pressKind_ = PressKind::Orbit;
  moveStarted_ = false;
  blockNoted_ = false;
  pressPartId_.clear();
  if (dragOverlayVisible_ || dragSnapX_ || dragSnapY_ || dragSnapZ_) {
    dragOverlayVisible_ = false;
    dragSnapX_ = false;
    dragSnapY_ = false;
    dragSnapZ_ = false;
    emit dragOverlayChanged();
  }
}

void ViewportItem::setStandardView(const QString& name) {
  orthographic_ = name != "iso";
  if (name == "top") {
    azimuth_ = 0.f;
    elevation_ = 89.f;
  } else if (name == "front") {
    azimuth_ = 0.f;
    elevation_ = 0.f;
  } else if (name == "right") {
    azimuth_ = 90.f;
    elevation_ = 0.f;
  } else {
    azimuth_ = 40.f;
    elevation_ = 28.f;
    orthographic_ = false;
  }
  fit(false);
}

void ViewportItem::deliverPick(const QString& id) {
  if (controller_ == nullptr) return;
  if (id.isEmpty()) {
    controller_->select(QString());
    return;
  }
  const auto scene = controller_->copyScene();
  for (const ScenePart& part : scene) {
    if (part.id == id) {
      controller_->select(id);
      return;
    }
  }
  controller_->select(QString());
}

void ViewportItem::mousePressEvent(QMouseEvent* event) {
  if (!isRealMouse(event)) return;
  forceActiveFocus();
  pressPos_ = event->position();
  lastPos_ = pressPos_;
  pressButton_ = event->button();
  dragging_ = false;
  if (pressButton_ == Qt::LeftButton)
    beginLeftPress(pressPos_, event->modifiers() & Qt::ShiftModifier);
  else
    pressKind_ = PressKind::Orbit;
  event->accept();
}

void ViewportItem::mouseMoveEvent(QMouseEvent* event) {
  if (!isRealMouse(event)) return;
  if ((event->position() - pressPos_).manhattanLength() > 4) dragging_ = true;
  if (!dragging_) {
    lastPos_ = event->position();
    return;
  }
  const bool panDrag = pressButton_ == Qt::MiddleButton || pressButton_ == Qt::RightButton ||
                       (pressButton_ == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier));
  if (panDrag) {
    pan(event->position() - lastPos_);
    lastPos_ = event->position();
  } else if (pressButton_ == Qt::LeftButton) {
    movePressedPart(event->position());
  }
  event->accept();
}

void ViewportItem::mouseReleaseEvent(QMouseEvent* event) {
  if (!isRealMouse(event)) return;
  if (pressButton_ == Qt::RightButton && !dragging_) {
    showContactMenu(event->position());
  } else if (pressButton_ == Qt::LeftButton) {
    finishPress(dragging_);
  } else if (!dragging_) {
    requestPick(event->position());
  }
  dragging_ = false;
  pressButton_ = Qt::NoButton;
  event->accept();
}

void ViewportItem::wheelEvent(QWheelEvent* event) {
  const int delta = event->angleDelta().y();
  if (delta == 0) return;
  zoomBy(delta > 0 ? 0.9f : 1.11f);
  event->accept();
}

void ViewportItem::touchEvent(QTouchEvent* event) {
  const auto points = event->points();
  if (points.size() >= 2) {
    const QPointF first = points[0].position();
    const QPointF second = points[1].position();
    const float distance = float(QLineF(first, second).length());
    const QPointF middle = (first + second) * 0.5;
    if (pinchActive_ && lastPinchDistance_ > 1.f && distance > 1.f) {
      zoomBy(lastPinchDistance_ / distance);
      pan(middle - lastPinchMid_);
    }
    lastPinchDistance_ = distance;
    lastPinchMid_ = middle;
    pinchActive_ = true;
    event->accept();
    return;
  }
  if (points.size() == 1) {
    const QEventPoint& point = points[0];
    if (point.state() == QEventPoint::Pressed) {
      touchOrigin_ = point.position();
      lastPos_ = touchOrigin_;
      touchMoved_ = false;
      pinchActive_ = false;
    } else if (point.state() == QEventPoint::Updated) {
      if ((point.position() - touchOrigin_).manhattanLength() > 8) touchMoved_ = true;
      if (touchMoved_) orbit(point.position() - lastPos_);
      lastPos_ = point.position();
    } else if (point.state() == QEventPoint::Released && !touchMoved_ && !pinchActive_) {
      requestPick(point.position());
    }
  }
  if (points.size() < 2 && event->isEndEvent()) pinchActive_ = false;
  event->accept();
}

void ViewportItem::focusOutEvent(QFocusEvent* event) {
  if (pressKind_ == PressKind::Move) controller_->cancelPartDrag();
  pressKind_ = PressKind::Orbit;
  moveStarted_ = false;
  pressPartId_.clear();
  if (dragOverlayVisible_ || dragSnapX_ || dragSnapY_ || dragSnapZ_) {
    dragOverlayVisible_ = false;
    dragSnapX_ = false;
    dragSnapY_ = false;
    dragSnapZ_ = false;
    emit dragOverlayChanged();
  }
  QQuickFramebufferObject::focusOutEvent(event);
}

}  // namespace timberlab::ui
