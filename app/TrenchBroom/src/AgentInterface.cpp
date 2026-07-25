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

#include "mdl/Map.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"

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
  else
  {
    log("ERR unknown command: " + line);
  }
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
  if (m_appController.openDocument(pathFromQString(path)))
  {
    log("OK open " + path);
  }
  else
  {
    log("ERR open failed: " + path);
  }
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
