// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

/// The settings stored in a project's `.freecad-project.json`.
struct AgenticExport ProjectSettings
{
    std::string name;
    std::string mainDocument;         ///< relative path of the main `.FCStd`, may be empty
    std::string addonFolder {"Mod"};  ///< per-project addon installs, put on the module path
    bool autoSave {true};
    bool autoRecompute {true};
    std::vector<std::string> registries;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& value);
};

/// A project is a folder. Its settings file is created on first open.
class AgenticExport Project
{
public:
    static constexpr const char* settingsFile = ".freecad-project.json";

    explicit Project(const std::filesystem::path& root);

    static bool exists(const std::filesystem::path& root);

    /// Reads the settings file if present, otherwise keeps the defaults.
    void load();
    /// Writes the settings file.
    void save() const;

    const std::filesystem::path& root() const;
    const ProjectSettings& settings() const;
    ProjectSettings& settings();

    /// `.FCStd` files directly in the project folder, sorted.
    std::vector<std::string> documents() const;

    /// Everything a client wants to know about the project.
    nlohmann::json info() const;

private:
    std::filesystem::path rootPath;
    ProjectSettings values;
};

}  // namespace Agentic
