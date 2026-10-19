#pragma once

#include "DocumentController.hpp"

#include <QFocusEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPointF>
#include <QQuickFramebufferObject>
#include <QRectF>
#include <QString>
#include <QTouchEvent>
#include <QVector3D>
#include <QWheelEvent>

namespace timberlab::ui {

class DocumentController;

class ViewportItem : public QQuickFramebufferObject {
  Q_OBJECT
  Q_PROPERTY(DocumentController* controller READ controller WRITE setController NOTIFY controllerChanged)
  Q_PROPERTY(bool dragOverlayVisible READ dragOverlayVisible NOTIFY dragOverlayChanged)
  Q_PROPERTY(qreal dragOverlayX READ dragOverlayX NOTIFY dragOverlayChanged)
  Q_PROPERTY(qreal dragOverlayY READ dragOverlayY NOTIFY dragOverlayChanged)
  Q_PROPERTY(qreal dragOverlayZ READ dragOverlayZ NOTIFY dragOverlayChanged)
  Q_PROPERTY(bool dragSnapX READ dragSnapX NOTIFY dragOverlayChanged)
  Q_PROPERTY(bool dragSnapY READ dragSnapY NOTIFY dragOverlayChanged)
  Q_PROPERTY(bool dragSnapZ READ dragSnapZ NOTIFY dragOverlayChanged)
  Q_PROPERTY(QPointF dragOverlayPosition READ dragOverlayPosition NOTIFY dragOverlayChanged)
 public:
  explicit ViewportItem(QQuickItem* parent = nullptr);

  Renderer* createRenderer() const override;

  DocumentController* controller() const { return controller_; }
  void setController(DocumentController* controller);

  bool dragOverlayVisible() const { return dragOverlayVisible_; }
  qreal dragOverlayX() const { return dragOverlayX_; }
  qreal dragOverlayY() const { return dragOverlayY_; }
  qreal dragOverlayZ() const { return dragOverlayZ_; }
  bool dragSnapX() const { return dragSnapX_; }
  bool dragSnapY() const { return dragSnapY_; }
  bool dragSnapZ() const { return dragSnapZ_; }
  QPointF dragOverlayPosition() const { return dragOverlayPosition_; }

  QVector3D eye() const;
  QVector3D target() const { return target_; }
  float distance() const { return distance_; }
  float azimuth() const { return azimuth_; }
  float elevation() const { return elevation_; }
  bool orthographic() const { return orthographic_; }
  QString selectedId() const;
  int pickSerial() const { return pickSerial_; }
  QPointF pickPoint() const { return pickPoint_; }
  bool pickPending() const { return pickPending_; }
  void clearPick() { pickPending_ = false; }

  Q_INVOKABLE void frameAll();
  Q_INVOKABLE void frameSelection();
  Q_INVOKABLE void setStandardView(const QString& name);

 public slots:
  void deliverPick(const QString& id);

 signals:
  void controllerChanged();
  void contactMenuRequested(qreal x, qreal y);
  void dragOverlayChanged();

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void touchEvent(QTouchEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

 private:
  enum class PressKind { Orbit, Move, Blocked };

  void orbit(QPointF delta);
  void pan(QPointF delta);
  void zoomBy(float factor);
  void requestPick(QPointF position);
  void fit(bool selectionOnly);
  void viewProjection(QMatrix4x4& view, QMatrix4x4& projection) const;
  bool rayThrough(QPointF position, QVector3D& origin, QVector3D& direction) const;
  bool projectToScreen(const QVector3D& world, QPointF& screen) const;
  bool planeHit(QPointF position, QVector3D& hit) const;
  bool screenBox(const ContactZone& zone, QRectF& rect) const;
  bool contactUnder(QPointF position, QString& idA, QString& idB) const;
  void beginLeftPress(QPointF position, bool shift);
  void movePressedPart(QPointF position);
  void showContactMenu(QPointF position);
  void finishPress(bool commitMove);

  DocumentController* controller_ = nullptr;
  QVector3D target_{400.f, 250.f, 200.f};
  float distance_ = 1800.f;
  float azimuth_ = 40.f;
  float elevation_ = 25.f;
  bool orthographic_ = false;
  QPointF pressPos_;
  QPointF lastPos_;
  Qt::MouseButton pressButton_ = Qt::NoButton;
  bool dragging_ = false;
  bool pickPending_ = false;
  int pickSerial_ = 0;
  QPointF pickPoint_;
  bool pinchActive_ = false;
  float lastPinchDistance_ = 0;
  QPointF lastPinchMid_;
  QPointF touchOrigin_;
  bool touchMoved_ = false;
  PressKind pressKind_ = PressKind::Orbit;
  bool moveStarted_ = false;
  bool blockNoted_ = false;
  QString pressPartId_;
  QVector3D planeNormal_;
  QVector3D planePoint_;
  QVector3D grabPoint_;
  bool dragOverlayVisible_ = false;
  qreal dragOverlayX_ = 0;
  qreal dragOverlayY_ = 0;
  qreal dragOverlayZ_ = 0;
  bool dragSnapX_ = false;
  bool dragSnapY_ = false;
  bool dragSnapZ_ = false;
  QPointF dragOverlayPosition_;
};

}  // namespace timberlab::ui
