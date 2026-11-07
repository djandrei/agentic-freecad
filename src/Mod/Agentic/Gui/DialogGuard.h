// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

/// Answers modal dialogs so that no command can block the main thread.
///
/// Installed on the application as an event filter. While agent mode is on, or
/// while a tool call is running, every modal QDialog that shows is closed on
/// the next event-loop turn: a QMessageBox with its default button (or its
/// escape button), a file dialog and any other dialog by rejection. What was
/// answered is noted on the runtime and returned with the tool result.
class AgenticGuiExport DialogGuard: public QObject
{
    Q_OBJECT

public:
    explicit DialogGuard(QObject* parent = nullptr);

    static void install();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

}  // namespace Agentic
