#pragma once

#include "BoardConfig.h"
#include "BoardRepository.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pulsepad {

class BoardPackageValidator {
public:
    explicit BoardPackageValidator(PackageLimits limits = {});

    void validate_path(const std::string& path) const;
    void validate_entries(const std::vector<PackageEntryInfo>& entries) const;
    bool is_sound_path(const std::string& path) const;

private:
    PackageLimits limits_;
};

void export_board_package_to_directory(const BoardRepository& repository, const std::filesystem::path& destination, const BoardState& state);
BoardState import_board_package_from_directory(const BoardRepository& repository, const std::filesystem::path& source, const PackageLimits& limits = {});

std::filesystem::path create_unique_import_temp_dir();
void remove_import_temp_dir(const std::filesystem::path& path);

} // namespace pulsepad

namespace pulsepad {

class ZipBoardPackage {
public:
    explicit ZipBoardPackage(BoardRepository& repository);

    void export_to(const std::filesystem::path& destination, const BoardState& state) const;
    BoardState import_from(const std::filesystem::path& source) const;

private:
    BoardRepository& repository_;
};

} // namespace pulsepad
