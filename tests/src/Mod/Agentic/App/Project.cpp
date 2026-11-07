// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <Mod/Agentic/App/EngineSession.h>
#include <Mod/Agentic/App/MessageLog.h>
#include <Mod/Agentic/App/Runtime.h>
#include <Mod/Agentic/App/Project.h>
#include <Mod/Agentic/App/SessionManager.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

class ProjectTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        root = fs::temp_directory_path() / fs::path("agentic-project-" + std::to_string(::getpid()));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    void TearDown() override
    {
        fs::remove_all(root);
    }
    fs::path root;
};

TEST_F(ProjectTest, defaultsAndRoundTrip)
{
    Agentic::Project project(root);
    EXPECT_EQ(project.settings().name, root.filename().string());
    EXPECT_TRUE(project.settings().autoSave);
    project.settings().mainDocument = "main.FCStd";
    project.settings().autoRecompute = false;
    project.save();
    EXPECT_TRUE(Agentic::Project::exists(root));

    Agentic::Project again(root);
    again.load();
    EXPECT_EQ(again.settings().mainDocument, "main.FCStd");
    EXPECT_FALSE(again.settings().autoRecompute);
    std::ofstream(root / "part.FCStd") << "x";
    std::ofstream(root / "notes.txt") << "x";
    EXPECT_EQ(again.documents(), (std::vector<std::string> {"part.FCStd"}));
    json info = again.info();
    EXPECT_EQ(info["root"], root.string());
    EXPECT_EQ(info["documents"][0], "part.FCStd");
}

namespace
{

struct Recorder: public Agentic::SessionObserver
{
    std::vector<std::string> events;
    void projectOpened(const Agentic::Project& p) override
    {
        events.push_back("opened:" + p.settings().name);
    }
    void currentProjectChanged(const Agentic::Project* p) override
    {
        events.push_back(std::string("current:") + (p ? p->settings().name : "none"));
    }
    void fileWritten(const Agentic::Project&, const Agentic::WriteResult& w) override
    {
        events.push_back("written:" + w.path);
    }
    void changeLogged(const Agentic::Project&, const Agentic::ChangeEntry& e) override
    {
        events.push_back("change:" + e.kind);
    }
};

}  // namespace

TEST_F(ProjectTest, sessionManagerOpensOnceAndNotifies)
{
    Agentic::ObserverList observers;
    Recorder recorder;
    observers.add(&recorder);
    Agentic::MessageLog messages;
    Agentic::SessionManager manager(observers, messages, Agentic::Runtime::instance());

    Agentic::EngineSession& a = manager.open(root, false);
    EXPECT_TRUE(Agentic::Project::exists(root));
    EXPECT_EQ(manager.current(), &a);
    EXPECT_EQ(&manager.open(root, false), &a);
    EXPECT_EQ(manager.list().size(), 1U);
    EXPECT_EQ(manager.find(root.filename().string()), &a);
    EXPECT_EQ(manager.find(root.string()), &a);
    EXPECT_EQ(manager.find("nothing"), nullptr);
    EXPECT_THROW(manager.open(root / "missing", false), std::invalid_argument);
    Agentic::EngineSession& b = manager.open(root / "created", true);
    EXPECT_EQ(manager.current(), &b);

    a.setCurrentTool("file_write");
    auto w = a.writeFile("s.py", "pass\n", std::nullopt);
    EXPECT_EQ(a.changes().since(1).size(), 1U);
    EXPECT_EQ(a.changes().since(1)[0].tool, "file_write");
    EXPECT_EQ(a.changes().since(1)[0].detail["version"], w.version);

    std::vector<std::string> expected = {
        "opened:" + root.filename().string(),
        "current:" + root.filename().string(),
        "opened:created",
        "current:created",
        "written:s.py",
        "change:file",
    };
    EXPECT_EQ(recorder.events, expected);
}
