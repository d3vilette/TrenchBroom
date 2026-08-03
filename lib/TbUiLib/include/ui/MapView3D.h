/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <QCursor>
#include <QPoint>

#include "base/NotifierConnection.h"
#include "ui/MapViewBase.h"

#include <filesystem>
#include <vector>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace tb
{
namespace gl
{
class PerspectiveCamera;
}

namespace ui
{
class FlyModeHelper;

class MapView3D : public MapViewBase
{
  Q_OBJECT
private:
  std::unique_ptr<gl::PerspectiveCamera> m_camera;
  std::unique_ptr<FlyModeHelper> m_flyModeHelper;
  bool m_ignoreCameraChangeEvents = false;

  // Noiuake: toggled mouse look (Mouse 4) — see noiuakeSetMouseLook
  bool m_noiuakeMouseLook = false;
  QPoint m_noiuakeMouseLookRestorePos;
  QCursor m_noiuakeMouseLookRestoreCursor;

  NotifierConnection m_notifierConnection;

public:
  MapView3D(AppController& appController, MapDocument& document, MapViewToolBox& toolBox);
  ~MapView3D() override;

  const gl::PerspectiveCamera& perspectiveCamera() const;

private:
  void initializeCamera();
  void initializeToolChain(MapViewToolBox& toolBox);

private: // notification
  void connectObservers();
  void cameraDidChange(const gl::Camera& camera);
  void preferenceDidChange(const std::filesystem::path& path);

protected: // QWidget overrides
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

protected: // QOpenGLWidget overrides
  void initializeGL() override;

private: // interaction events
  void bindEvents();

private: // other events
  void updateFlyMode();
  void resetFlyModeKeys();

  // Noiuake: toggled mouse look. Mouse 4 latches the camera into the same look
  // mode you normally get by holding the right button, so you can fly with
  // WASD hands-free; Mouse 4 again (or Escape, or losing focus) releases it.
  // While latched the pointer is hidden and pinned to the centre of the view,
  // so looking never runs out of desk the way a right-drag does.
  void noiuakeSetMouseLook(bool active);

private: // implement ToolBoxConnector interface
  PickRequest pickRequest(float x, float y) const override;
  mdl::PickResult pick(const vm::ray3d& pickRay) const override;

private: // implement RenderView interface
  void updateViewport(int x, int y, int width, int height) override;

private: // implement MapView interface
  vm::vec3d pasteObjectsDelta(
    const vm::bbox3d& bounds, const vm::bbox3d& referenceBounds) const override;

  bool canSelectTall() override;
  void selectTall() override;

  void reset2dCameras(const gl::Camera& masterCamera, bool animate) override;
  void focusCameraOnSelection(bool animate) override;

  vm::vec3f focusCameraOnObjectsPosition(const std::vector<mdl::Node*>& nodes);

  void moveCameraToPosition(const vm::vec3f& position, bool animate) override;
  void animateCamera(
    const vm::vec3f& position,
    const vm::vec3f& direction,
    const vm::vec3f& up,
    float zoom,
    int duration = DefaultCameraAnimationDuration);

  void moveCameraToCurrentTracePoint() override;

private: // implement MapViewBase interface
  gl::Camera& camera() override;

  vm::vec3d moveDirection(vm::direction direction) const override;
  size_t flipAxis(vm::direction direction) const override;
  vm::vec3d computePointEntityPosition(const vm::bbox3d& bounds) const override;

  ActionContext::Type viewActionContext() const override;

  void preRender() override;
  render::RenderMode renderMode() override;

  void renderMap(
    render::MapRenderer& renderer,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) override;
  void renderTools(
    MapViewToolBox& toolBox,
    render::RenderContext& renderContext,
    render::RenderBatch& renderBatch) override;

  void beforePopupMenu() override;

public: // override MapViewBase
  void cancel() override;

public: // implement CameraLinkableView interface
  void linkCamera(CameraLinkHelper& linkHelper) override;
};

} // namespace ui
} // namespace tb
