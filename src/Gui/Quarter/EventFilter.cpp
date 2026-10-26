/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

/*!  \class SIM::Coin3D::Quarter::EventFilter EventFilter.h Quarter/eventhandlers/EventFilter.h

*/

#include <memory>

#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>

#include "QuarterWidget.h"
#include "devices/Keyboard.h"
#include "devices/Mouse.h"
#include "devices/SpaceNavigatorDevice.h"
#include "eventhandlers/EventFilter.h"


namespace SIM { namespace Coin3D { namespace Quarter {

namespace {

QPointF getLocalPosition(const QMouseEvent* event)
{
  return event->position();
}

}

class EventFilterP {
public:
  QList<InputDevice *> devices;
  QuarterWidget * quarterwidget;
  QPoint globalmousepos;
  SbVec2s windowsize;

  bool coalescemoves = false;
  std::unique_ptr<QMouseEvent> pendingmove;

  bool translate(QEvent * event)
  {
    // make sure every device has updated screen size and mouse position
    // before translating events
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
      this->trackPointerPosition(dynamic_cast<QMouseEvent *>(event));
      break;
    case QEvent::Resize:
      this->trackWindowSize(dynamic_cast<QResizeEvent *>(event));
      break;
    default:
      break;
    }

    Q_FOREACH(InputDevice * device, this->devices) {
      const SoEvent * soevent = device->translateEvent(event);
      if (soevent && this->quarterwidget->processSoEvent(soevent)) {
        return true;
      }
    }
    return false;
  }

  void processPendingMove()
  {
    if (this->pendingmove) {
      const std::unique_ptr<QMouseEvent> move = std::move(this->pendingmove);
      this->translate(move.get());
    }
  }

  static bool appliesPendingMove(QEvent::Type type)
  {
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::Leave:
    case QEvent::ContextMenu:
    case QEvent::Resize:
    case QEvent::NativeGesture:
    case QEvent::Gesture:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
      return true;
    default:
      return false;
    }
  }

  void trackWindowSize(QResizeEvent * event)
  {
    this->windowsize = SbVec2s(event->size().width(),
                               event->size().height());

    Q_FOREACH(InputDevice * device, this->devices) {
      device->setWindowSize(this->windowsize);
    }
  }

  void trackPointerPosition(QMouseEvent * event)
  {
    assert(this->windowsize[1] != -1);
    this->globalmousepos = event->globalPosition().toPoint();

    SbVec2s mousepos = InputDevice::toDevicePixelPosition(
        getLocalPosition(event),
        this->windowsize,
        quarterwidget->devicePixelRatio());
    Q_FOREACH(InputDevice * device, this->devices) {
      device->setMousePosition(mousepos);
    }
  }
};

#define PRIVATE(obj) obj->pimpl

}}} // namespace

using namespace SIM::Coin3D::Quarter;

EventFilter::EventFilter(QObject * parent)
  : QObject(parent)
{
  PRIVATE(this) = new EventFilterP;

  QuarterWidget* quarter = dynamic_cast<QuarterWidget *>(parent);
  PRIVATE(this)->quarterwidget = quarter;
  assert(PRIVATE(this)->quarterwidget);

  PRIVATE(this)->windowsize = SbVec2s(PRIVATE(this)->quarterwidget->width(),
                                      PRIVATE(this)->quarterwidget->height());

  PRIVATE(this)->coalescemoves =
      QGuiApplication::platformName().startsWith(QLatin1String("wayland"));

  PRIVATE(this)->devices += new Mouse(quarter);
  PRIVATE(this)->devices += new Keyboard(quarter);

#ifdef HAVE_SPACENAV_LIB
  PRIVATE(this)->devices += new SpaceNavigatorDevice(quarter);
#endif // HAVE_SPACENAV_LIB

}

EventFilter::~EventFilter()
{
  qDeleteAll(PRIVATE(this)->devices);
  delete PRIVATE(this);
}

/*!
  Adds a device for event translation
 */
void 
EventFilter::registerInputDevice(InputDevice * device)
{
  PRIVATE(this)->devices += device;
}

/*!
  Removes a device from event translation
 */
void 
EventFilter::unregisterInputDevice(InputDevice * device)
{
  int i = PRIVATE(this)->devices.indexOf(device);
  if (i != -1) {
    PRIVATE(this)->devices.removeAt(i);
  }
}

/*! Translates Qt Events into Coin events and passes them on to the
  event QuarterWidget for processing. If the event cannot be
  translated or processed, it is forwarded to Qt and the method
  returns false.
 */
bool
EventFilter::eventFilter(QObject * obj, QEvent * qevent)
{
  Q_UNUSED(obj); 
  if (PRIVATE(this)->coalescemoves) {
    if (qevent->type() == QEvent::MouseMove) {
      auto * move = static_cast<QMouseEvent *>(qevent);
      PRIVATE(this)->globalmousepos = move->globalPosition().toPoint();
      const bool scheduled = PRIVATE(this)->pendingmove != nullptr;
      PRIVATE(this)->pendingmove.reset(move->clone());
      if (!scheduled) {
        QMetaObject::invokeMethod(
            this, [this]() { PRIVATE(this)->processPendingMove(); },
            Qt::QueuedConnection);
      }
      return true;
    }
    if (EventFilterP::appliesPendingMove(qevent->type())) {
      PRIVATE(this)->processPendingMove();
    }
  }

  // translate QEvent into SoEvent and see if it is handled by scene
  // graph
  return PRIVATE(this)->translate(qevent);
}

/*!
  Returns mouse position in global coordinates
 */
const QPoint &
EventFilter::globalMousePosition() const
{
  return PRIVATE(this)->globalmousepos;
}

#undef PRIVATE
