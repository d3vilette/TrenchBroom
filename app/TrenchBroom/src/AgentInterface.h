/*
 Copyright (C) 2026 Noiuake project

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

#include <QObject>
#include <QString>
#include <QTimer>

namespace tb::ui
{
class AppController;

/**
 * File-based automation interface for driving TrenchBroom from an external
 * agent (mirrors the Noiuake game engine's agent_cmds.txt protocol).
 *
 * Enabled only when the TB_AGENT_DIR environment variable points at a
 * directory. Polls <dir>/tb_agent_cmds.txt every 500ms; each line is a
 * command, and the file is deleted after reading (execute-then-delete
 * handshake). Every command and result is appended to <dir>/tb_agent.log.
 *
 * Commands:
 *   status              write <dir>/tb_agent_status.txt (TB_STATUS/TB_DOC lines)
 *   screenshot <path>   save a PNG grab of each open map window
 *   open <path>         open a .map document
 *   camera <x> <y> <z> <pitch> <yaw>
 *                       warp the top map window's 3D viewport camera to the
 *                       given position/angles (Quake convention: +pitch =
 *                       looking down, degrees). Fed by the game's tb_look.
 */
// no Q_OBJECT: new-style connects don't need the metaobject, and the app
// target doesn't run moc
class AgentInterface : public QObject
{
private:
  AppController& m_appController;
  QString m_dir;
  QTimer m_timer;

public:
  explicit AgentInterface(AppController& appController, QObject* parent = nullptr);

  bool isEnabled() const;

private:
  void poll();
  void execute(const QString& line);

  void cmdStatus();
  void cmdScreenshot(const QString& path);
  void cmdOpen(const QString& path);
  void cmdCamera(const QString& args);

  void log(const QString& message);
};

} // namespace tb::ui
