// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <vector>

#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Macro.h>

#include <Mod/Agentic/App/DocumentEngine.h>
#include <Mod/Agentic/App/EngineSession.h>
#include <Mod/Agentic/App/MessageLog.h>
#include <Mod/Agentic/App/ObjectTracker.h>
#include <Mod/Agentic/App/Runtime.h>
#include <Mod/Agentic/App/SessionManager.h>

#include "CommandEngine.h"
#include "TaskPanelEngine.h"

using json = nlohmann::json;

namespace Agentic
{

CommandEngine::CommandEngine(Runtime& runtime)
    : runtime(runtime)
{}

bool CommandEngine::isActive(const std::string& name)
{
    Gui::CommandManager& manager = Gui::Application::Instance->commandManager();
    Gui::Command* command = manager.getCommandByName(name.c_str());
    if (!command) {
        throw std::invalid_argument("no command named " + name + " (call command_list)");
    }
    command->initAction();
    return command->isActive();
}

json CommandEngine::run(const std::string& name)
{
    Gui::CommandManager& manager = Gui::Application::Instance->commandManager();
    if (!isActive(name)) {
        throw std::runtime_error(
            name
            + " is not active now: it needs a document, a selection or a workbench state "
              "it does not have (see command_info and selection_set)"
        );
    }

    runtime.takeDialogs();
    const std::uint64_t messagesFrom = runtime.messages().nextSeq();
    std::vector<std::string> lines;
    json objects;
    {
        Gui::MacroManager::MacroRedirector redirect(
            [&lines](Gui::MacroManager::LineType type, const char* line) {
                if (type != Gui::MacroManager::Cmt) {
                    lines.emplace_back(line);
                }
            }
        );
        ObjectTracker tracker;
        manager.runCommandByName(name.c_str());
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        objects = tracker.toJson();
    }

    json result = {
        {"command", name},
        {"python", lines},
        {"objects", objects},
        {"taskPanel", TaskPanelEngine::describe()},
        {"dialogs", runtime.takeDialogs()},
    };

    json messages = json::array();
    int errors = 0;
    int warnings = 0;
    for (const auto& entry : runtime.messages().since(messagesFrom)) {
        if (entry.level == "log") {
            continue;
        }
        messages.push_back({{"level", entry.level}, {"text", entry.text}});
        if (entry.level == "error" || entry.level == "critical") {
            ++errors;
        }
        else if (entry.level == "warning") {
            ++warnings;
        }
    }
    result["messages"] = messages;
    result["errors"] = errors;
    result["warnings"] = warnings;

    if (EngineSession* session = runtime.sessions().current()) {
        std::string python;
        for (const auto& line : lines) {
            python += line + "\n";
        }
        ChangeEntry entry;
        entry.kind = "command";
        entry.summary = "ran command " + name;
        if (!result["taskPanel"].is_null()) {
            entry.summary += " (task panel open)";
        }
        if (lines.empty()) {
            entry.summary += " (no Python recorded)";
        }
        entry.python = python;
        entry.detail = {{"command", name}, {"objects", objects}, {"recorded", !lines.empty()}};
        session->recordChange(std::move(entry));
        if (result["taskPanel"].is_null()) {
            result["saved"] = session->documents().saveProjectDocuments();
        }
        else {
            result["saved"] = json::array();  // saved when the panel is accepted
        }
    }
    return result;
}

}  // namespace Agentic
