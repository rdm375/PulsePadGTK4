#include "BoardPackage.h"
#include "BoardRepository.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace pulsepad;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr << "\n"; ++failures; } } while (0)
#define CHECK_EQ(a,b) do { auto va=(a); auto vb=(b); if (!(va == vb)) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b << " (" << va << " vs " << vb << ")\n"; ++failures; } } while (0)
#define CHECK_THROWS(expr) do { bool threw=false; try { (void)(expr); } catch (...) { threw=true; } if (!threw) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_THROWS failed: " #expr << "\n"; ++failures; } } while (0)
#define CHECK_NOTHROW(expr) do { try { (void)(expr); } catch (const std::exception& ex) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_NOTHROW failed: " #expr << " threw " << ex.what() << "\n"; ++failures; } } while (0)

class TempDir {
public:
    explicit TempDir(const std::string& prefix) {
        path_ = fs::temp_directory_path() / (prefix + "-XXXXXX");
        auto writable = path_.string();
        char* made = ::mkdtemp(writable.data());
        if (!made) throw std::runtime_error("mkdtemp failed");
        path_ = made;
    }
    ~TempDir() { std::error_code ignored; fs::remove_all(path_, ignored); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

static void write_file(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

static std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static BoardState package_board() {
    BoardState state;
    state.gridRows = 1;
    state.gridColumns = 2;
    state.masterVolumeDb = -6.0f;
    state.buttons = default_buttons(2);
    state.buttons[0].id = 0;
    state.buttons[0].label = "Kick";
    state.buttons[0].originalFilename = "kick.wav";
    state.buttons[0].storedFilename = "kick.wav";
    state.buttons[0].relativePathOrAssetReference = "sounds/kick.wav";
    state.buttons[0].color = "red";
    state.buttons[0].midiChannel = 1;
    state.buttons[0].midiNote = 36;
    state.buttons[0].playbackMode = PlaybackMode::Loop;
    state.buttons[0].volume = 0.5f;
    state.buttons[0].assigned = true;
    state.buttons[1].id = 1;
    state.buttons[1].label = "Empty";
    return state;
}

static void test_repository_save_load_round_trip() {
    TempDir root("pulsepad-repo-test");
    BoardRepository repo(root.path());
    auto state = package_board();
    repo.save(state);
    auto loaded = repo.load();
    CHECK_EQ(loaded.gridRows, 1);
    CHECK_EQ(loaded.gridColumns, 2);
    CHECK_EQ(loaded.buttons.size(), 2u);
    CHECK_EQ(loaded.buttons[0].label, "Kick");
    CHECK_EQ(loaded.buttons[0].relativePathOrAssetReference, "sounds/kick.wav");
    CHECK_EQ(loaded.buttons[0].color, "red");
    CHECK_EQ(loaded.buttons[0].midiChannel, 1);
    CHECK_EQ(loaded.buttons[0].midiNote, 36);
    CHECK(loaded.buttons[0].playbackMode == PlaybackMode::Loop);
}

static void test_package_directory_round_trip_copies_assets() {
    TempDir sourceRoot("pulsepad-package-source");
    TempDir packageRoot("pulsepad-package-export");
    TempDir importRoot("pulsepad-package-import");
    BoardRepository sourceRepo(sourceRoot.path());
    BoardRepository importRepo(importRoot.path());
    auto state = package_board();
    write_file(sourceRepo.sound_file(state.buttons[0]), "fake-wav-data");

    export_board_package_to_directory(sourceRepo, packageRoot.path(), state);
    CHECK(fs::exists(packageRoot.path() / "soundboard.json"));
    CHECK_EQ(read_file(packageRoot.path() / "sounds" / "kick.wav"), "fake-wav-data");

    auto imported = import_board_package_from_directory(importRepo, packageRoot.path());
    CHECK_EQ(imported.gridRows, state.gridRows);
    CHECK_EQ(imported.gridColumns, state.gridColumns);
    CHECK_EQ(imported.buttons[0].label, "Kick");
    CHECK_EQ(imported.buttons[0].storedFilename, "kick.wav");
    CHECK_EQ(imported.buttons[0].relativePathOrAssetReference, "sounds/kick.wav");
    CHECK_EQ(read_file(importRepo.sound_file(imported.buttons[0])), "fake-wav-data");
}

static void test_missing_audio_asset_fails_cleanly() {
    TempDir sourceRoot("pulsepad-package-missing-source");
    TempDir packageRoot("pulsepad-package-missing-export");
    TempDir importRoot("pulsepad-package-missing-import");
    BoardRepository sourceRepo(sourceRoot.path());
    BoardRepository importRepo(importRoot.path());
    auto state = package_board();
    CHECK_THROWS(export_board_package_to_directory(sourceRepo, packageRoot.path(), state));

    state.buttons[0].assigned = true;
    write_file(packageRoot.path() / "soundboard.json", encode_board_config(state));
    CHECK_THROWS(import_board_package_from_directory(importRepo, packageRoot.path()));
}

static void test_zip_entry_safety_regressions() {
    PackageLimits limits;
    limits.maxEntries = 3;
    limits.maxSingleFileBytes = 5;
    limits.maxTotalBytes = 12;

    CHECK_NOTHROW(validate_package_entries_or_throw({{"soundboard.json", 2}, {"sounds/a.wav", 5}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"../evil.wav", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"/tmp/evil.wav", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"sounds/a.wav", 1}, {"sounds/b.wav", 1}, {"sounds/c.wav", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"sounds/a.wav", 6}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 5}, {"sounds/a.wav", 5}, {"sounds/b.wav", 5}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"sounds/a.wav", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"sounds/a.wav", 1}, {"sounds/a.wav", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"sounds\\evil.wav", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"sounds/link/evil.wav", 1}}, limits));
}

static void test_unsafe_package_directory_does_not_write_outside_destination() {
    TempDir packageRoot("pulsepad-package-unsafe");
    TempDir importRoot("pulsepad-package-safe-import");
    TempDir outsideRoot("pulsepad-package-outside");
    BoardRepository importRepo(importRoot.path());
    auto outside = outsideRoot.path() / "evil.wav";
    write_file(packageRoot.path() / "soundboard.json", encode_board_config(package_board()));
    write_file(packageRoot.path() / "sounds" / "..evil.wav", "not traversal but unsupported hidden-ish path");
    CHECK_THROWS(import_board_package_from_directory(importRepo, packageRoot.path()));
    CHECK(!fs::exists(outside));
    CHECK(!fs::exists(importRoot.path() / "evil.wav"));
}

int main() {
    test_repository_save_load_round_trip();
    test_package_directory_round_trip_copies_assets();
    test_missing_audio_asset_fails_cleanly();
    test_zip_entry_safety_regressions();
    test_unsafe_package_directory_does_not_write_outside_destination();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All package tests passed\n";
    return 0;
}
