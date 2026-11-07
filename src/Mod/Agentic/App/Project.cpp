// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <algorithm>
#include <fstream>

#include "Project.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Agentic
{

json ProjectSettings::toJson() const
{
    return {
        {"name", name},
        {"mainDocument", mainDocument.empty() ? json() : json(mainDocument)},
        {"addonFolder", addonFolder},
        {"autoSave", autoSave},
        {"autoRecompute", autoRecompute},
        {"registries", registries},
    };
}

void ProjectSettings::fromJson(const json& value)
{
    if (!value.is_object()) {
        throw std::runtime_error("project settings must be a JSON object");
    }
    if (value.contains("name") && value["name"].is_string()) {
        name = value["name"].get<std::string>();
    }
    if (value.contains("mainDocument")) {
        mainDocument = value["mainDocument"].is_string() ? value["mainDocument"].get<std::string>()
                                                         : std::string();
    }
    if (value.contains("addonFolder") && value["addonFolder"].is_string()) {
        addonFolder = value["addonFolder"].get<std::string>();
    }
    if (value.contains("autoSave") && value["autoSave"].is_boolean()) {
        autoSave = value["autoSave"].get<bool>();
    }
    if (value.contains("autoRecompute") && value["autoRecompute"].is_boolean()) {
        autoRecompute = value["autoRecompute"].get<bool>();
    }
    if (value.contains("registries") && value["registries"].is_array()) {
        registries.clear();
        for (const auto& item : value["registries"]) {
            if (item.is_string()) {
                registries.push_back(item.get<std::string>());
            }
        }
    }
}

Project::Project(const fs::path& root)
    : rootPath(fs::weakly_canonical(root))
{
    values.name = rootPath.filename().string();
}

bool Project::exists(const fs::path& root)
{
    return fs::is_regular_file(root / settingsFile);
}

void Project::load()
{
    const fs::path file = rootPath / settingsFile;
    if (!fs::is_regular_file(file)) {
        return;
    }
    std::ifstream in(file);
    json value = json::parse(in, nullptr, false);
    if (value.is_discarded()) {
        throw std::runtime_error(std::string(settingsFile) + " is not valid JSON");
    }
    values.fromJson(value);
}

void Project::save() const
{
    const fs::path file = rootPath / settingsFile;
    const fs::path temp = rootPath / (std::string(settingsFile) + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot write " + file.string());
        }
        out << values.toJson().dump(2) << '\n';
    }
    fs::rename(temp, file);
}

const fs::path& Project::root() const
{
    return rootPath;
}

const ProjectSettings& Project::settings() const
{
    return values;
}

ProjectSettings& Project::settings()
{
    return values;
}

std::vector<std::string> Project::documents() const
{
    std::vector<std::string> docs;
    for (const auto& entry : fs::directory_iterator(rootPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext == ".fcstd") {
            docs.push_back(entry.path().filename().string());
        }
    }
    std::sort(docs.begin(), docs.end());
    return docs;
}

json Project::info() const
{
    json value = values.toJson();
    value["root"] = rootPath.string();
    value["documents"] = documents();
    return value;
}

}  // namespace Agentic
