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

#include "AgentInterface.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QSaveFile>
#include <QTextStream>

#include "gl/Camera.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "mdl/NodeQueries.h"
#include "mdl/Selection.h"
#include "mdl/WorldNode.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapView3D.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

#include <cmath>

namespace tb::ui
{
namespace
{
constexpr auto PollIntervalMs = 500;

QString timestamp()
{
  return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}
} // namespace

AgentInterface::AgentInterface(AppController& appController, QObject* parent)
  : QObject{parent}
  , m_appController{appController}
  , m_dir{qEnvironmentVariable("TB_AGENT_DIR")}
{
  if (isEnabled())
  {
    connect(&m_timer, &QTimer::timeout, this, &AgentInterface::poll);
    m_timer.start(PollIntervalMs);
    log("agent interface enabled");
  }
}

bool AgentInterface::isEnabled() const
{
  return !m_dir.isEmpty() && QDir{m_dir}.exists();
}

void AgentInterface::poll()
{
  const auto cmdPath = QDir{m_dir}.filePath("tb_agent_cmds.txt");
  auto file = QFile{cmdPath};
  if (!file.exists() || !file.open(QFile::ReadOnly | QFile::Text))
  {
    return;
  }

  const auto contents = QTextStream{&file}.readAll();
  file.close();
  // execute-then-delete handshake: remove before executing so a crash during
  // execution can't replay the same batch forever
  file.remove();

  const auto lines = contents.split('\n', Qt::SkipEmptyParts);
  for (const auto& line : lines)
  {
    execute(line.trimmed());
  }
}

void AgentInterface::execute(const QString& line)
{
  if (line.isEmpty())
  {
    return;
  }

  log("EXEC " + line);

  const auto space = line.indexOf(' ');
  const auto cmd = space < 0 ? line : line.left(space);
  const auto arg = space < 0 ? QString{} : line.mid(space + 1).trimmed();

  if (cmd == "status")
  {
    cmdStatus();
  }
  else if (cmd == "screenshot" && !arg.isEmpty())
  {
    cmdScreenshot(arg);
  }
  else if (cmd == "open" && !arg.isEmpty())
  {
    cmdOpen(arg);
  }
  else if (cmd == "camera" && !arg.isEmpty())
  {
    cmdCamera(arg);
  }
  else if (cmd == "selection")
  {
    cmdSelection();
  }
  else if (cmd == "entities")
  {
    cmdEntities();
  }
  else if (cmd == "reload")
  {
    cmdReload();
  }
  else
  {
    log("ERR unknown command: " + line);
  }
}

/* true if any open document has unsaved changes; optionally reports which.
Agent commands that would discard the in-memory document (open, reload) must
check this -- the matching UI actions get a confirmation dialog, but a
file-protocol caller gets no dialog, so the guard has to live here (board
#46/#49, KNOWN_ISSUES 2026-07-27). */
bool AgentInterface::anyDocumentModified(QString* modifiedPath) const
{
  for (const auto* window : m_appController.mapWindowManager().mapWindows())
  {
    const auto& map = window->document().map();
    if (map.modified())
    {
      if (modifiedPath)
      {
        *modifiedPath = pathAsQPath(map.path());
      }
      return true;
    }
  }
  return false;
}

void AgentInterface::cmdStatus()
{
  auto file = QSaveFile{QDir{m_dir}.filePath("tb_agent_status.txt")};
  if (!file.open(QFile::WriteOnly | QFile::Text))
  {
    log("ERR status: cannot write status file");
    return;
  }

  auto out = QTextStream{&file};
  const auto windows = m_appController.mapWindowManager().mapWindows();
  out << "TB_STATUS time=" << timestamp() << " windows=" << windows.size() << "\n";

  for (const auto* window : windows)
  {
    const auto& map = window->document().map();
    out << "TB_DOC path=" << pathAsQPath(map.path()) << " persistent="
        << (map.persistent() ? 1 : 0) << " modified=" << (map.modified() ? 1 : 0)
        << " active=" << (window->isActiveWindow() ? 1 : 0) << "\n";
  }

  file.commit();
  log("OK status");
}

void AgentInterface::cmdScreenshot(const QString& path)
{
  const auto windows = m_appController.mapWindowManager().mapWindows();
  if (windows.empty())
  {
    log("ERR screenshot: no map windows open");
    return;
  }

  auto index = 0;
  for (auto* window : windows)
  {
    // for multiple windows, suffix _1, _2, ... before the extension
    auto target = path;
    if (index > 0)
    {
      const auto dot = path.lastIndexOf('.');
      target = dot < 0 ? path + QString{"_%1"}.arg(index)
                       : path.left(dot) + QString{"_%1"}.arg(index) + path.mid(dot);
    }

    if (window->grab().save(target))
    {
      log("OK screenshot " + target);
    }
    else
    {
      log("ERR screenshot: failed to save " + target);
    }
    ++index;
  }
}

void AgentInterface::cmdOpen(const QString& path)
{
  // on Windows SDI, openDocument() reuses the open window and replaces its
  // document with NO unsaved-changes prompt (unlike the Revert menu action)
  auto modifiedPath = QString{};
  if (anyDocumentModified(&modifiedPath))
  {
    log("ERR open: document has unsaved changes: " + modifiedPath);
    return;
  }

  if (m_appController.openDocument(pathFromQString(path)))
  {
    log("OK open " + path);
  }
  else
  {
    log("ERR open failed: " + path);
  }
}

void AgentInterface::cmdSelection()
{
  const auto* window = m_appController.mapWindowManager().topMapWindow();
  if (!window)
  {
    log("ERR selection: no map window open");
    return;
  }

  auto file = QSaveFile{QDir{m_dir}.filePath("tb_agent_selection.txt")};
  if (!file.open(QFile::WriteOnly | QFile::Text))
  {
    log("ERR selection: cannot write selection file");
    return;
  }

  auto out = QTextStream{&file};
  const auto& selection = window->document().map().selection();
  auto count = 0;

  for (const auto* entityNode : selection.entities)
  {
    const auto& entity = entityNode->entity();
    const auto& origin = entity.origin();
    out << "TB_SEL classname=" << QString::fromStdString(entity.classname())
        << " origin=(" << origin.x() << " " << origin.y() << " " << origin.z()
        << ")\n";
    ++count;
  }

  for (const auto* brushNode : selection.brushes)
  {
    // a selected brush's useful identity is its parent entity + where it is
    const auto* parent = brushNode->entity();
    const auto center = brushNode->logicalBounds().center();
    out << "TB_SEL_BRUSH parent="
        << QString::fromStdString(
             parent ? parent->entity().classname() : std::string{"?"})
        << " center=(" << center.x() << " " << center.y() << " " << center.z()
        << ")\n";
    ++count;
  }

  out << "TB_SEL_DONE count=" << count << "\n";
  file.commit();
  log(QString{"OK selection count=%1"}.arg(count));
}

void AgentInterface::cmdEntities()
{
  auto* window = m_appController.mapWindowManager().topMapWindow();
  if (!window)
  {
    log("ERR entities: no map window open");
    return;
  }

  auto file = QSaveFile{QDir{m_dir}.filePath("tb_agent_entities.txt")};
  if (!file.open(QFile::WriteOnly | QFile::Text))
  {
    log("ERR entities: cannot write entities file");
    return;
  }

  auto out = QTextStream{&file};
  auto& worldNode = window->document().map().worldNode();
  auto count = 0;

  // worldspawn (the WorldNode) is itself an EntityNodeBase, so the root
  // vector plus a descendant filter yields every entity incl. brush entities
  auto roots = std::vector<mdl::Node*>{&worldNode};
  const auto nodes = mdl::collectDescendants(roots);
  const auto emit_entity = [&](const mdl::EntityNodeBase& entityNode) {
    const auto& entity = entityNode.entity();
    const auto& origin = entity.origin();
    out << "TB_ENT classname=" << QString::fromStdString(entity.classname())
        << " origin=(" << origin.x() << " " << origin.y() << " " << origin.z()
        << ")\n";
    ++count;
  };

  emit_entity(worldNode);
  for (const auto* node : nodes)
  {
    if (const auto* entityNode = dynamic_cast<const mdl::EntityNodeBase*>(node);
        entityNode && entityNode != &worldNode)
    {
      emit_entity(*entityNode);
    }
  }

  out << "TB_ENT_DONE count=" << count << "\n";
  file.commit();
  log(QString{"OK entities count=%1"}.arg(count));
}

void AgentInterface::cmdReload()
{
  auto* window = m_appController.mapWindowManager().topMapWindow();
  if (!window)
  {
    log("ERR reload: no map window open");
    return;
  }

  const auto& map = window->document().map();
  if (map.modified())
  {
    log("ERR reload: document has unsaved changes: " + pathAsQPath(map.path()));
    return;
  }

  const auto path = pathAsQPath(map.path());
  auto ok = true;
  window->document().reload() | kdl::transform_error([&](const auto& e) {
    ok = false;
    log("ERR reload failed: " + QString::fromStdString(e.msg));
  });
  if (ok)
  {
    log("OK reload " + path);
  }
}

void AgentInterface::cmdCamera(const QString& args)
{
  const auto parts = args.split(' ', Qt::SkipEmptyParts);
  if (parts.size() != 5)
  {
    log("ERR camera: expected <x> <y> <z> <pitch> <yaw>");
    return;
  }

  float v[5];
  for (auto i = 0; i < 5; ++i)
  {
    auto ok = false;
    v[i] = parts[i].toFloat(&ok);
    if (!ok)
    {
      log("ERR camera: bad number: " + parts[i]);
      return;
    }
  }

  auto* window = m_appController.mapWindowManager().topMapWindow();
  if (!window)
  {
    log("ERR camera: no map window open");
    return;
  }
  auto* view3D = window->findChild<MapView3D*>();
  if (!view3D)
  {
    log("ERR camera: no 3D view in the top map window");
    return;
  }

  // Quake angle convention (AngleVectors): positive pitch looks DOWN
  const auto pitch = v[3] * float(M_PI) / 180.0f;
  const auto yaw = v[4] * float(M_PI) / 180.0f;
  const auto dir = vm::vec3f{
    std::cos(pitch) * std::cos(yaw),
    std::cos(pitch) * std::sin(yaw),
    -std::sin(pitch)};

  auto& camera = static_cast<MapViewBase*>(view3D)->camera();
  camera.moveTo(vm::vec3f{v[0], v[1], v[2]});
  camera.setDirection(vm::normalize(dir), vm::vec3f{0.0f, 0.0f, 1.0f});

  // deliberately no raise()/activateWindow(): the owner is usually in the
  // game when tb_look fires; TB updates quietly in the background
  log(QString{"OK camera %1 %2 %3 / pitch %4 yaw %5"}
        .arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3]).arg(v[4]));
}

void AgentInterface::log(const QString& message)
{
  auto file = QFile{QDir{m_dir}.filePath("tb_agent.log")};
  if (file.open(QFile::WriteOnly | QFile::Append | QFile::Text))
  {
    QTextStream{&file} << timestamp() << " " << message << "\n";
  }
}

} // namespace tb::ui
