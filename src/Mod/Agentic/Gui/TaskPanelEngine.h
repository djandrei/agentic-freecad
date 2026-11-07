// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

class QWidget;

namespace Gui::TaskView
{
class TaskDialog;
}

namespace Agentic
{

/// The agent's hands on a task panel.
///
/// Most modelling commands open a task panel and wait for a person to fill
/// fields and press OK. describe() lists the panel's input widgets as fields
/// (id, kind, label, value, options), set() fills them through the widgets'
/// own API so the usual signals fire, accept() and reject() press OK or
/// Cancel through Gui::Control.
class AgenticGuiExport TaskPanelEngine
{
public:
    /// The open task panel as JSON, or null when none is open.
    static nlohmann::json describe();
    static nlohmann::json set(const nlohmann::json& fields);
    static nlohmann::json accept();
    static nlohmann::json reject();

private:
    static Gui::TaskView::TaskDialog* requireDialog();
    static nlohmann::json describeDialog(Gui::TaskView::TaskDialog* dialog);
};

}  // namespace Agentic
