#include "BoardPackage.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace pulsepad {

namespace {

std::string read_text_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Could not read package manifest");
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void copy_file_checked(const fs::path& from, const fs::path& to) {
    fs::create_directories(to.parent_path());
    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) throw std::runtime_error("Could not copy package audio asset");
}

std::vector<PackageEntryInfo> collect_directory_entries(const fs::path& root) {
    std::vector<PackageEntryInfo> entries;
    if (!fs::exists(root)) throw std::runtime_error("Package directory does not exist");
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        auto relative = fs::relative(entry.path(), root).generic_string();
        entries.push_back({relative, static_cast<std::uint64_t>(entry.file_size())});
    }
    return entries;
}

} // namespace

BoardPackageValidator::BoardPackageValidator(PackageLimits limits) : limits_(limits) {}
void BoardPackageValidator::validate_path(const std::string& path) const { validate_package_path_or_throw(path); }
void BoardPackageValidator::validate_entries(const std::vector<PackageEntryInfo>& entries) const { validate_package_entries_or_throw(entries, limits_); }
bool BoardPackageValidator::is_sound_path(const std::string& path) const { return is_valid_sound_entry(path); }

void export_board_package_to_directory(const BoardRepository& repository, const fs::path& destination, const BoardState& state) {
    fs::remove_all(destination);
    fs::create_directories(destination / "sounds");
    const auto manifest = encode_board_config(state);
    {
        std::ofstream out(destination / "soundboard.json", std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Could not write package manifest");
        out.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
        if (!out) throw std::runtime_error("Could not write package manifest");
    }
    for (const auto& button : state.buttons) {
        if (!button.assigned || button.storedFilename.empty()) continue;
        validate_package_path_or_throw("sounds/" + button.storedFilename);
        auto source = repository.sound_file(button);
        if (!fs::exists(source)) throw std::runtime_error("Missing source audio file: " + button.storedFilename);
        copy_file_checked(source, destination / "sounds" / button.storedFilename);
    }
}

BoardState import_board_package_from_directory(const BoardRepository& repository, const fs::path& source, const PackageLimits& limits) {
    const auto entries = collect_directory_entries(source);
    validate_package_entries_or_throw(entries, limits);

    fs::path manifestPath;
    if (fs::exists(source / "soundboard.json")) manifestPath = source / "soundboard.json";
    else if (fs::exists(source / "board.json")) manifestPath = source / "board.json";
    else throw std::runtime_error("Missing manifest");

    BoardState state = decode_board_config(read_text_file(manifestPath));
    std::set<std::string> importedFiles;
    for (const auto& entry : entries) {
        if (is_valid_sound_entry(entry.path)) importedFiles.insert(entry.path.substr(7));
    }

    fs::create_directories(repository.sounds_dir());
    for (auto& button : state.buttons) {
        if (!button.assigned) continue;
        validate_package_path_or_throw("sounds/" + button.storedFilename);
        if (importedFiles.count(button.storedFilename) == 0) throw std::runtime_error("Missing imported audio asset: " + button.storedFilename);
        copy_file_checked(source / "sounds" / button.storedFilename, repository.sounds_dir() / button.storedFilename);
        button.relativePathOrAssetReference = "sounds/" + button.storedFilename;
    }
    repository.save(state);
    return state;
}

fs::path create_unique_import_temp_dir() {
    fs::path pattern = fs::temp_directory_path() / "pulsepad-gtk-import-XXXXXX";
    std::string writable = pattern.string();
    char* made = ::mkdtemp(writable.data());
    if (!made) throw std::runtime_error("Could not create temporary import directory");
    return fs::path(made);
}

void remove_import_temp_dir(const fs::path& path) {
    if (path.empty()) return;
    std::error_code ignored;
    fs::remove_all(path, ignored);
}

} // namespace pulsepad
