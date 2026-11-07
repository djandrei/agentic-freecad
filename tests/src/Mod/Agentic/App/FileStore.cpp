// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <Mod/Agentic/App/FileStore.h>

namespace fs = std::filesystem;

class FileStoreTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        root = fs::temp_directory_path()
            / fs::path("agentic-filestore-" + std::to_string(::getpid()));
        fs::remove_all(root);
        fs::create_directories(root / "sub");
        fs::create_directories(root / ".hidden");
        outside = fs::temp_directory_path()
            / fs::path("agentic-outside-" + std::to_string(::getpid()));
        fs::create_directories(outside);
        std::ofstream(outside / "secret.txt") << "no";
        fs::create_directory_symlink(outside, root / "link");
        store = std::make_unique<Agentic::FileStore>(root);
    }

    void TearDown() override
    {
        store.reset();
        fs::remove_all(root);
        fs::remove_all(outside);
    }

    fs::path root;
    fs::path outside;
    std::unique_ptr<Agentic::FileStore> store;
};

TEST_F(FileStoreTest, writeCreatesAndReadsBack)
{
    auto w = store->write("sub/a.py", "print(1)\n", std::nullopt);
    EXPECT_TRUE(w.created);
    EXPECT_EQ(w.startLine, 1);
    EXPECT_EQ(w.linesAdded, 1);
    EXPECT_EQ(w.linesRemoved, 0);
    auto r = store->read("sub/a.py");
    EXPECT_TRUE(r.isText);
    EXPECT_EQ(r.content, "print(1)\n");
    EXPECT_EQ(r.version, w.version);
    EXPECT_EQ(r.path, "sub/a.py");
}

TEST_F(FileStoreTest, hunkDescribesTheChange)
{
    store->write("f.txt", "a\nb\nc\nd\n", std::nullopt);
    auto w = store->write("f.txt", "a\nB\nX\nc\nd\n", std::nullopt);
    EXPECT_FALSE(w.created);
    EXPECT_EQ(w.startLine, 2);
    EXPECT_EQ(w.linesRemoved, 1);
    EXPECT_EQ(w.linesAdded, 2);
    auto same = store->write("f.txt", "a\nB\nX\nc\nd\n", std::nullopt);
    EXPECT_EQ(same.startLine, 0);
}

TEST_F(FileStoreTest, baseVersionConflict)
{
    auto w = store->write("f.txt", "one\n", std::nullopt);
    store->write("f.txt", "two\n", std::nullopt);
    EXPECT_THROW(store->write("f.txt", "three\n", w.version), Agentic::ConflictError);
    EXPECT_THROW(store->write("new.txt", "x", std::string("abc")), Agentic::ConflictError);
    auto current = store->read("f.txt");
    EXPECT_NO_THROW(store->write("f.txt", "three\n", current.version));
}

TEST_F(FileStoreTest, editsReplaceExactlyOnce)
{
    store->write("f.py", "x = 1\ny = 2\n", std::nullopt);
    auto w = store->edit("f.py", {{"y = 2", "y = 3"}}, std::nullopt);
    EXPECT_EQ(store->read("f.py").content, "x = 1\ny = 3\n");
    EXPECT_EQ(w.startLine, 2);
    EXPECT_THROW(store->edit("f.py", {{"nope", "x"}}, std::nullopt), std::invalid_argument);
    store->write("f.py", "a\na\n", std::nullopt);
    EXPECT_THROW(store->edit("f.py", {{"a", "b"}}, std::nullopt), std::invalid_argument);
}

TEST_F(FileStoreTest, escapesAreRefused)
{
    EXPECT_THROW(store->resolve("../x", false), std::invalid_argument);
    EXPECT_THROW(store->resolve("sub/../../x", false), std::invalid_argument);
    EXPECT_THROW(store->read("link/secret.txt"), std::invalid_argument);
    EXPECT_THROW(store->write("link/new.txt", "x", std::nullopt), std::invalid_argument);
    EXPECT_THROW(store->resolve(outside.string(), false), std::invalid_argument);
    EXPECT_NO_THROW(store->resolve((root / "sub").string(), true));
}

TEST_F(FileStoreTest, listingSkipsHiddenAndSorts)
{
    store->write("b.txt", "b", std::nullopt);
    store->write("sub/a.txt", "a", std::nullopt);
    std::ofstream(root / ".hidden" / "h.txt") << "h";
    std::ofstream(root / ".dotfile") << "d";
    auto entries = store->list("", false);
    std::vector<std::string> paths;
    for (const auto& e : entries) {
        paths.push_back(e.path);
    }
    EXPECT_EQ(paths, (std::vector<std::string> {"link", "sub", "b.txt"}));
    auto recursive = store->list("", true);
    bool sawNested = false;
    for (const auto& e : recursive) {
        EXPECT_NE(e.path.find(".hidden"), 0U);
        sawNested = sawNested || e.path == "sub/a.txt";
    }
    EXPECT_TRUE(sawNested);
}

TEST_F(FileStoreTest, renameAndRemove)
{
    store->write("f.txt", "f", std::nullopt);
    store->rename("f.txt", "sub/g.txt");
    EXPECT_FALSE(fs::exists(root / "f.txt"));
    EXPECT_EQ(store->read("sub/g.txt").content, "f");
    EXPECT_THROW(store->remove("sub"), std::invalid_argument);
    store->remove("sub/g.txt");
    store->remove("sub");
    EXPECT_FALSE(fs::exists(root / "sub"));
    EXPECT_THROW(store->remove(""), std::invalid_argument);
}

TEST_F(FileStoreTest, binaryIsDetected)
{
    std::string bytes = "PK\x03\x04";
    bytes.push_back('\0');
    bytes += "rest";
    store->write("doc.FCStd", bytes, std::nullopt);
    auto r = store->read("doc.FCStd");
    EXPECT_FALSE(r.isText);
    EXPECT_TRUE(r.content.empty());
    EXPECT_EQ(r.size, bytes.size());
}
