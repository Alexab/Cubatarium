#include "mainwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QMessageBox>
#include <QCoreApplication>

#include <cmath>
#include "User.h"

namespace cutum {

MainWidget::MainWidget(QWidget* parent, Qt::WindowFlags f)
 : QOpenGLWidget(parent, f)
{
 IsRunned = false;
 setAutoFillBackground(false);
 setFocusPolicy(Qt::StrongFocus);
}

MainWidget::~MainWidget()
{
 IsRunned = false;
 if(Painter)
 {
  if(geometries)
   geometries->SetPainter(nullptr);
  Painter.reset();
 }
 makeCurrent();
 doneCurrent();
}

void MainWidget::Init(std::shared_ptr<Core> core_, std::shared_ptr<World> world_, std::shared_ptr<GeometryEngine> geometries_,
     std::shared_ptr<ViewEngine> views_)
{
 core = core_;
 geometries = geometries_;
 views = views_;
 WorldInstance = world_;
}

void MainWidget::mousePressEvent(QMouseEvent *e)
{
 // Save mouse press position
 mousePressPosition = QVector2D(e->localPos());
 if(e->button() == Qt::RightButton)
 {
  WorldInstance->GetCurrentUserCamera()->ResetMouseMove(e->localPos().x(), e->localPos().y());
  is_mouse_pressed = true;
 }
 else
 if(e->button() == Qt::LeftButton)
 {
  LeftMousePressed = std::chrono::steady_clock::now();
  is_left_mouse_button_pressed = true;
 }
}

void MainWidget::mouseMoveEvent(QMouseEvent *e)
{
 if(!is_mouse_pressed)
  return;
 WorldInstance->GetCurrentUserCamera()->UpdateMouseMove(WorldInstance, e->localPos().x(), e->localPos().y());
}

void MainWidget::mouseReleaseEvent(QMouseEvent *e)
{
 if(e->button() == Qt::RightButton)
 {
  is_mouse_pressed = false;
 }
 else
 if(e->button() == Qt::LeftButton)
 {
  is_left_mouse_button_pressed = false;
  double delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-LeftMousePressed).count() / 1000.0;
  if(delta_time < 0.5)
   WorldInstance->AddObjectByView();
  else
   WorldInstance->DelObjectByView();
 }
}

void MainWidget::keyPressEvent(QKeyEvent *event)
{
 WorldInstance->GetCurrentUserCamera()->UpdateKeyStatus(event->key(), true);
 if(event->key() >= Qt::Key::Key_0 && event->key() <= Qt::Key::Key_9)
  WorldInstance->GetCurrentUser()->SetActiveObjectTypeNameByIndex(event->key()-48);

 if(event->key() == Qt::Key::Key_F12)
 {
  QMessageBox::StandardButton reply = QMessageBox::question(this, "Warning", "Reset current world?",
                                  QMessageBox::Yes|QMessageBox::No);
  if (reply == QMessageBox::Yes)
  {
   WorldInstance->Create(WorldInstance->GetWorldName());
  }
 }
 else
 if(event->key() == Qt::Key::Key_Delete)
 {
  WorldInstance->DelObjectByView();
 }
}

void MainWidget::keyReleaseEvent(QKeyEvent *event)
{
 WorldInstance->GetCurrentUserCamera()->UpdateKeyStatus(event->key(), false);
}

void MainWidget::focusOutEvent(QFocusEvent* event)
{
 views->ResetAllKeyStatus();
}


void MainWidget::timerEvent(QTimerEvent *)
{
 update();
}

void MainWidget::initializeGL()
{
 initializeOpenGLFunctions();
 glClearColor(0, 0, 0, 1);

 if(!geometries->InitEngine())
  close();

 //WorldInstance->GenerateUsers();
}

void MainWidget::Run()
{
 Painter = std::make_shared<QPainter>(this);
 Painter->setBackgroundMode(Qt::BGMode::TransparentMode);
 Painter->setCompositionMode(QPainter::CompositionMode::CompositionMode_SourceOver);
 geometries->SetPainter(Painter);

 IsRunned = true;
 timer.start(12, this);
}

void MainWidget::resizeGL(int w, int h)
{
 // Calculate aspect ratio
 qreal aspect = qreal(w) / qreal(h ? h : 1);
 views->GetActiveCamera()->SetAspectRatio(aspect);
 if(IsRunned)
 {
  Painter->end();
  Painter->begin(this);
 }
}

void MainWidget::paintGL()
{
 if(!IsRunned)
  return;
 views->UpdateFrameTime();
 WorldInstance->DoMovement();
 geometries->Paint(width(), height());
}

}
