#include "BoardPackage.h"

#include <zip.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace pulsepad {
namespace {
constexpr zip_uint64_t kMaxImportManifestBytes = 2 * 1024 * 1024;

void add_text(zip_t* archive, const std::string& name, const std::string& text) {
    void* buffer = std::malloc(text.size());
    if (!buffer) throw std::runtime_error("Export write failure");
    std::memcpy(buffer, text.data(), text.size());
    zip_source_t* source = zip_source_buffer(archive, buffer, text.size(), 1);
    if (!source) {
        std::free(buffer);
        throw std::runtime_error("Export write failure");
    }
    if (zip_file_add(archive, name.c_str(), source, ZIP_FL_OVERWRITE) < 0) {
        zip_source_free(source);
        throw std::runtime_error("Export write failure");
    }
}

void add_file(zip_t* archive, const std::string& name, const fs::path& path) {
    zip_source_t* source = zip_source_file(archive, path.string().c_str(), 0, 0);
    if (!source) throw std::runtime_error("Export write failure");
    if (zip_file_add(archive, name.c_str(), source, ZIP_FL_OVERWRITE) < 0) {
        zip_source_free(source);
        throw std::runtime_error("Export write failure");
    }
}

zip_uint64_t checked_entry_size(zip_t* archive, zip_uint64_t index) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat_index(archive, index, 0, &st) != 0 || !(st.valid & ZIP_STAT_SIZE)) throw std::runtime_error("Could not read import package");
    return st.size;
}

std::string read_entry_limited(zip_t* archive, zip_uint64_t index, zip_uint64_t maxBytes) {
    const zip_uint64_t size = checked_entry_size(archive, index);
    if (size > maxBytes) throw std::runtime_error("Import manifest is too large");
    zip_file_t* zf = zip_fopen_index(archive, index, 0);
    if (!zf) throw std::runtime_error("Could not read import package");
    std::string data(static_cast<std::size_t>(size), '\0');
    zip_uint64_t offset = 0;
    while (offset < size) {
        const zip_uint64_t remaining = size - offset;
        const zip_uint64_t chunk = std::min<zip_uint64_t>(remaining, 64 * 1024);
        const zip_int64_t nread = zip_fread(zf, data.data() + offset, chunk);
        if (nread <= 0) {
            zip_fclose(zf);
            throw std::runtime_error("Could not read import package");
        }
        offset += static_cast<zip_uint64_t>(nread);
    }
    zip_fclose(zf);
    return data;
}

void copy_entry_to_file(zip_t* archive, zip_uint64_t index, const fs::path& out, zip_uint64_t maxBytes) {
    const zip_uint64_t size = checked_entry_size(archive, index);
    if (size > maxBytes) throw std::runtime_error("Imported audio file is too large");
    zip_file_t* zf = zip_fopen_index(archive, index, 0);
    if (!zf) throw std::runtime_error("Could not read import package");
    fs::create_directories(out.parent_path());
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    if (!file) {
        zip_fclose(zf);
        throw std::runtime_error("Could not write imported audio asset");
    }
    std::array<char, 64 * 1024> buffer{};
    zip_uint64_t total = 0;
    while (total < size) {
        const zip_uint64_t remaining = size - total;
        const zip_uint64_t want = std::min<zip_uint64_t>(remaining, buffer.size());
        const zip_int64_t nread = zip_fread(zf, buffer.data(), want);
        if (nread <= 0) {
            zip_fclose(zf);
            throw std::runtime_error("Could not read import package");
        }
        total += static_cast<zip_uint64_t>(nread);
        file.write(buffer.data(), nread);
        if (!file) {
            zip_fclose(zf);
            throw std::runtime_error("Could not write imported audio asset");
        }
    }
    zip_fclose(zf);
}
} // namespace

ZipBoardPackage::ZipBoardPackage(BoardRepository& repository) : repository_(repository) {}

void ZipBoardPackage::export_to(const fs::path& destination, const BoardState& state) const {
    int err = 0;
    zip_t* archive = zip_open(destination.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!archive) throw std::runtime_error("Could not create export package");
    try {
        add_text(archive, "soundboard.json", encode_board_config(state));
        for (const auto& button : state.buttons) {
            if (!button.assigned || button.storedFilename.empty()) continue;
            const auto entry = "sounds/" + button.storedFilename;
            validate_package_path_or_throw(entry);
            const auto file = repository_.sound_file(button);
            if (!fs::exists(file)) throw std::runtime_error("Missing source audio file: " + button.storedFilename);
            add_file(archive, entry, file);
        }
        if (zip_close(archive) != 0) throw std::runtime_error("Could not finalize export package");
    } catch (...) {
        zip_discard(archive);
        throw;
    }
}

BoardState ZipBoardPackage::import_from(const fs::path& source) const {
    int err = 0;
    zip_t* archive = zip_open(source.string().c_str(), ZIP_RDONLY, &err);
    if (!archive) throw std::runtime_error("Could not open import package");
    fs::path temp;
    try {
        temp = create_unique_import_temp_dir();
        std::string manifestText;
        std::vector<PackageEntryInfo> entries;
        std::set<std::string> importedFiles;
        const zip_int64_t count = zip_get_num_entries(archive, 0);
        if (count < 0) throw std::runtime_error("Could not read import package");
        for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(count); ++i) {
            const char* rawName = zip_get_name(archive, i, 0);
            if (!rawName) continue;
            const std::string name(rawName);
            const zip_uint64_t entrySize = checked_entry_size(archive, i);
            entries.push_back({name, static_cast<std::uint64_t>(entrySize)});
            validate_package_path_or_throw(name);
            if (name == "soundboard.json" || name == "board.json") {
                manifestText = read_entry_limited(archive, i, kMaxImportManifestBytes);
            } else if (is_valid_sound_entry(name)) {
                const auto filename = name.substr(7);
                copy_entry_to_file(archive, i, temp / filename, PackageLimits{}.maxSingleFileBytes);
                importedFiles.insert(filename);
            }
        }
        validate_package_entries_or_throw(entries);
        if (manifestText.empty()) throw std::runtime_error("Missing manifest");
        BoardState state = decode_board_config(manifestText);
        for (auto& button : state.buttons) {
            if (!button.assigned) continue;
            validate_package_path_or_throw("sounds/" + button.storedFilename);
            if (importedFiles.count(button.storedFilename) == 0) throw std::runtime_error("Missing imported audio asset: " + button.storedFilename);
            button.relativePathOrAssetReference = "sounds/" + button.storedFilename;
            repository_.normalize_trim_to_duration(button);
        }
        repository_.clear_all_audio_assets();
        fs::create_directories(repository_.sounds_dir());
        for (const auto& entry : fs::directory_iterator(temp)) {
            fs::copy_file(entry.path(), repository_.sounds_dir() / entry.path().filename(), fs::copy_options::overwrite_existing);
        }
        state.status = "Import successful";
        repository_.save(state);
        zip_close(archive);
        remove_import_temp_dir(temp);
        return state;
    } catch (...) {
        zip_close(archive);
        if (!temp.empty()) remove_import_temp_dir(temp);
        throw;
    }
}

} // namespace pulsepad
