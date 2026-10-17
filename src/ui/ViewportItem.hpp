#pragma once

#include <QMouseEvent>
#include <QPointF>
#include <QQuickFramebufferObject>
#include <QString>
#include <QTouchEvent>
#include <QVector3D>
#include <QWheelEvent>

namespace timberlab::ui {

class DocumentController;

class ViewportItem : public QQuickFramebufferObject {
  Q_OBJECT
  Q_PROPERTY(DocumentController* controller READ controller WRITE setController NOTIFY controllerChanged)
 public:
  explicit ViewportItem(QQuickItem* parent = nullptr);

  Renderer* createRenderer() const override;

  DocumentController* controller() const { return controller_; }
  void setController(DocumentController* controller);

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

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void touchEvent(QTouchEvent* event) override;

 private:
  void orbit(QPointF delta);
  void pan(QPointF delta);
  void zoomBy(float factor);
  void requestPick(QPointF position);
  void fit(bool selectionOnly);

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
};

}  // namespace timberlab::ui
