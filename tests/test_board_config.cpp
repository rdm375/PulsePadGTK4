#include "BoardConfig.h"
#include "PadGridPresenter.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

using namespace pulsepad;

static int failures = 0;

#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr << "\n"; ++failures; } } while (0)
#define CHECK_EQ(a,b) do { auto va=(a); auto vb=(b); if (!(va == vb)) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b << " (" << va << " vs " << vb << ")\n"; ++failures; } } while (0)
#define CHECK_NEAR(a,b,eps) do { auto va=(a); auto vb=(b); if (std::fabs(va - vb) > (eps)) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR failed: " #a " ~= " #b << " (" << va << " vs " << vb << ")\n"; ++failures; } } while (0)
#define CHECK_THROWS(expr) do { bool threw=false; try { (void)(expr); } catch (...) { threw=true; } if (!threw) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_THROWS failed: " #expr << "\n"; ++failures; } } while (0)
#define CHECK_NOTHROW(expr) do { try { (void)(expr); } catch (const std::exception& ex) { std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_NOTHROW failed: " #expr << " threw " << ex.what() << "\n"; ++failures; } } while (0)

static BoardState populated_board() {
    BoardState s;
    s.gridRows = 2;
    s.gridColumns = 2;
    s.masterVolumeDb = -12.0f;
    s.themeMode = AppThemeMode::Dark;
    s.midiEnabled = true;
    s.midiPortName = "MIDI Port A";
    s.buttons = default_buttons(4);

    s.buttons[0].label = "Kick";
    s.buttons[0].originalFilename = "kick drum.wav";
    s.buttons[0].storedFilename = "button-0-token-kick_drum.wav";
    s.buttons[0].relativePathOrAssetReference = "sounds/button-0-token-kick_drum.wav";
    s.buttons[0].mimeType = "audio/wav";
    s.buttons[0].color = "red";
    s.buttons[0].volume = 1.5f;
    s.buttons[0].playbackSpeed = 0.75f;
    s.buttons[0].playbackPitch = 1.25f;
    s.buttons[0].pan = -0.25f;
    s.buttons[0].trimStart = 0.125;
    s.buttons[0].trimEnd = 2.5;
    s.buttons[0].durationSeconds = 3.0;
    s.buttons[0].fadeInMs = 10;
    s.buttons[0].fadeOutMs = 20;
    s.buttons[0].exclusiveGroup = "Drums";
    s.buttons[0].hotkeyKeyval = 'z';
    s.buttons[0].hotkeyLabel = "z";
    s.buttons[0].midiChannel = 10;
    s.buttons[0].midiNote = 36;
    s.buttons[0].playbackMode = PlaybackMode::Retrigger;
    s.buttons[0].playbackDirection = PlaybackDirection::Reverse;
    s.buttons[0].assigned = true;
    s.buttons[0].userRenamed = true;

    PadGroup drums;
    drums.name = "Drums";
    drums.type = PadGroupType::Exclusive;
    drums.color = "blue";
    drums.transition = GroupTransition::Crossfade;
    drums.fadeOutMs = 250;
    drums.fadeInMs = 300;
    drums.duckEnabled = true;
    drums.duckTargets.insert("Music");
    drums.duckAmountDb = 9.0f;
    drums.duckAttackMs = 25;
    drums.duckReleaseMs = 500;
    s.groups.push_back(drums);
    return s;
}

static void test_round_trip_populated_board() {
    auto s = populated_board();
    auto decoded = decode_board_config(encode_board_config(s));
    CHECK_EQ(decoded.gridRows, 2);
    CHECK_EQ(decoded.gridColumns, 2);
    CHECK_NEAR(decoded.masterVolumeDb, -12.0f, 0.0001f);
    CHECK(decoded.themeMode == AppThemeMode::Dark);
    CHECK_EQ(decoded.midiPortName, "MIDI Port A");
    CHECK_EQ(decoded.buttons.size(), 4u);
    const auto& b = decoded.buttons[0];
    CHECK_EQ(b.label, "Kick");
    CHECK_EQ(b.originalFilename, "kick drum.wav");
    CHECK_EQ(b.storedFilename, "button-0-token-kick_drum.wav");
    CHECK_EQ(b.relativePathOrAssetReference, "sounds/button-0-token-kick_drum.wav");
    CHECK_EQ(b.mimeType, "audio/wav");
    CHECK_EQ(b.color, "red");
    CHECK_NEAR(b.volume, 1.5f, 0.0001f);
    CHECK_NEAR(b.playbackSpeed, 0.75f, 0.0001f);
    CHECK_NEAR(b.playbackPitch, 1.25f, 0.0001f);
    CHECK_NEAR(b.pan, -0.25f, 0.0001f);
    CHECK_NEAR(b.trimStart, 0.125, 0.0001);
    CHECK_NEAR(b.trimEnd, 2.5, 0.0001);
    CHECK_NEAR(b.durationSeconds, 3.0, 0.0001);
    CHECK_EQ(b.fadeInMs, 10);
    CHECK_EQ(b.fadeOutMs, 20);
    CHECK_EQ(b.exclusiveGroup, "Drums");
    CHECK_EQ(b.hotkeyKeyval, static_cast<std::uint32_t>('z'));
    CHECK_EQ(b.hotkeyLabel, "z");
    CHECK_EQ(b.midiChannel, 10);
    CHECK_EQ(b.midiNote, 36);
    CHECK(b.playbackMode == PlaybackMode::Retrigger);
    CHECK(b.playbackDirection == PlaybackDirection::Reverse);
    CHECK(b.assigned);
    CHECK(b.userRenamed);
    CHECK_EQ(decoded.groups.size(), 1u);
    CHECK_EQ(decoded.groups[0].name, "Drums");
    CHECK(decoded.groups[0].type == PadGroupType::Exclusive);
    CHECK(decoded.groups[0].transition == GroupTransition::Crossfade);
    CHECK(decoded.groups[0].duckTargets.count("Music") == 1);
}

static void test_empty_and_minimal_configs() {
    auto def = decode_board_config("{\"buttons\":[]}");
    CHECK_EQ(def.gridRows, 3);
    CHECK_EQ(def.gridColumns, 3);
    CHECK_EQ(def.buttons.size(), 9u);
    CHECK_EQ(def.buttons[0].label, "Pad 1");
    CHECK_EQ(def.masterVolumeDb, 0.0f);
    CHECK(def.themeMode == AppThemeMode::Light);

    auto old = decode_board_config("{\"grid\":{\"rows\":1,\"columns\":1},\"buttons\":[{\"id\":0,\"label\":\"Old\",\"filename_or_asset_id\":\"old.wav\",\"loop\":true,\"group\":\"Legacy\",\"assigned\":true}],\"future_field\":{\"ignored\":true}}");
    CHECK_EQ(old.buttons.size(), 1u);
    CHECK_EQ(old.buttons[0].storedFilename, "old.wav");
    CHECK_EQ(old.buttons[0].relativePathOrAssetReference, "sounds/old.wav");
    CHECK(old.buttons[0].playbackMode == PlaybackMode::Loop);
    CHECK_EQ(old.groups.size(), 1u);
    CHECK_EQ(old.groups[0].name, "Legacy");
}


static void test_playback_config_migration() {
    auto missing = decode_board_config("{\"grid\":{\"rows\":1,\"columns\":1},\"buttons\":[{\"id\":0,\"label\":\"No Playback Fields\"}]}");
    CHECK(missing.buttons[0].playbackMode == PlaybackMode::PlayThrough);
    CHECK_NEAR(missing.buttons[0].volume, 1.0f, 0.0001f);
    CHECK_NEAR(missing.buttons[0].trimStart, 0.0, 0.0001);
    CHECK_NEAR(missing.buttons[0].trimEnd, 0.0, 0.0001);

    auto loopFalse = decode_board_config("{\"grid\":{\"rows\":1,\"columns\":1},\"buttons\":[{\"id\":0,\"loop\":false}]}");
    CHECK(loopFalse.buttons[0].playbackMode == PlaybackMode::PlayThrough);

    auto loopTrue = decode_board_config("{\"grid\":{\"rows\":1,\"columns\":1},\"buttons\":[{\"id\":0,\"loop\":true}]}");
    CHECK(loopTrue.buttons[0].playbackMode == PlaybackMode::Loop);

    auto clamped = decode_board_config("{\"grid\":{\"rows\":1,\"columns\":1},\"buttons\":[{\"id\":0,\"volume\":-9,\"trim_start\":8,\"trim_end\":3}]}");
    CHECK_NEAR(clamped.buttons[0].volume, 0.0f, 0.0001f);
    CHECK_NEAR(clamped.buttons[0].trimStart, 8.0, 0.0001);
    CHECK_NEAR(clamped.buttons[0].trimEnd, 0.0, 0.0001);
}

static void test_invalid_values_default_or_clamp() {
    auto s = decode_board_config("{\"grid\":{\"rows\":99,\"columns\":0},\"master_volume_db\":99,\"theme_mode\":\"Future\",\"buttons\":[{\"id\":0,\"color\":\"chartreuse\",\"volume\":99,\"playback_speed\":-1.5,\"playback_pitch\":99,\"pan\":-9,\"trim_start\":-1,\"trim_end\":0.5,\"fade_in_ms\":999999,\"midi_channel\":99,\"midi_note\":300}]}");
    CHECK_EQ(s.gridRows, 8);
    CHECK_EQ(s.gridColumns, 1);
    CHECK_NEAR(s.masterVolumeDb, pulsepad::VOLUME_MAX_DB, 0.0001f);
    CHECK(s.themeMode == AppThemeMode::Light);
    CHECK_EQ(s.buttons[0].color, "default");
    CHECK_NEAR(s.buttons[0].volume, 4.0f, 0.0001f);
    CHECK_NEAR(s.buttons[0].playbackSpeed, 1.5f, 0.0001f);
    CHECK(s.buttons[0].playbackDirection == PlaybackDirection::Reverse);
    CHECK_NEAR(s.buttons[0].playbackPitch, 2.0f, 0.0001f);
    CHECK_NEAR(s.buttons[0].pan, -1.0f, 0.0001f);
    CHECK_NEAR(s.buttons[0].trimStart, 0.0, 0.0001);
    CHECK_EQ(s.buttons[0].fadeInMs, 10000);
    CHECK_EQ(s.buttons[0].midiChannel, -1);
    CHECK_EQ(s.buttons[0].midiNote, -1);
}


static void test_normalization_config_and_helpers() {
    auto s = populated_board();
    auto& b = s.buttons[0];
    b.normalizationMode = NormalizationMode::TrimmedRegion;
    b.measuredLufs = -23.0;
    b.measuredPeakDb = -4.0;
    b.normalizationGainDb = 3.0;
    b.analysisRegionStart = 0.125;
    b.analysisRegionEnd = 2.5;
    b.analysisSourceFile = "sounds/kick.wav";
    b.analysisSourceTimestamp = "12345";
    b.analysisValid = true;
    auto decoded = decode_board_config(encode_board_config(s));
    const auto& out = decoded.buttons[0];
    CHECK(out.normalizationMode == NormalizationMode::TrimmedRegion);
    CHECK_NEAR(out.measuredLufs, -23.0, 0.0001);
    CHECK_NEAR(out.measuredPeakDb, -4.0, 0.0001);
    CHECK_NEAR(out.normalizationGainDb, 3.0, 0.0001);
    CHECK_NEAR(out.analysisRegionStart, 0.125, 0.0001);
    CHECK_NEAR(out.analysisRegionEnd, 2.5, 0.0001);
    CHECK_EQ(out.analysisSourceFile, "sounds/kick.wav");
    CHECK(out.analysisValid);
}

static void test_normalization_mode_ui_ids_parse() {
    CHECK(normalization_mode_from_string("off") == NormalizationMode::Off);
    CHECK(normalization_mode_from_string("trimmed") == NormalizationMode::TrimmedRegion);
    CHECK(normalization_mode_from_string("entire") == NormalizationMode::EntireFile);
    CHECK(normalization_mode_from_string("TrimmedRegion") == NormalizationMode::TrimmedRegion);
    CHECK(normalization_mode_from_string("EntireFile") == NormalizationMode::EntireFile);
}

static void test_normalization_regions_and_invalidation() {
    SoundButton b = default_button(0);
    b.durationSeconds = 10.0;
    b.trimStart = 2.0;
    b.trimEnd = 6.0;
    b.normalizationMode = NormalizationMode::TrimmedRegion;
    auto trimmed = normalization_analysis_region(b);
    CHECK_NEAR(trimmed.first, 2.0, 0.0001);
    CHECK_NEAR(trimmed.second, 6.0, 0.0001);

    b.normalizationMode = NormalizationMode::EntireFile;
    auto full = normalization_analysis_region(b);
    CHECK_NEAR(full.first, 0.0, 0.0001);
    CHECK_NEAR(full.second, 10.0, 0.0001);

    b.normalizationMode = NormalizationMode::TrimmedRegion;
    b.trimStart = 0.0;
    b.trimEnd = 0.0;
    auto untrimmed = normalization_analysis_region(b);
    CHECK_NEAR(untrimmed.first, 0.0, 0.0001);
    CHECK_NEAR(untrimmed.second, 10.0, 0.0001);

    b.analysisValid = true;
    b.analysisFailed = true;
    b.measuredLufs = -20.0;
    b.normalizationGainDb = 2.0;
    invalidate_normalization_analysis(b);
    CHECK(!b.analysisValid);
    CHECK(!b.analysisFailed);
    CHECK_NEAR(b.normalizationGainDb, 0.0, 0.0001);
}

static void test_normalization_gain_is_peak_limited() {
    CHECK_NEAR(calculate_normalization_gain_db(-18.0, -24.0, -1.0, -10.0), 6.0, 0.0001);
    CHECK_NEAR(calculate_normalization_gain_db(-18.0, -30.0, -1.0, -3.0), 2.0, 0.0001);
    SoundButton b = default_button(0);
    b.normalizationMode = NormalizationMode::TrimmedRegion;
    b.analysisValid = true;
    b.normalizationGainDb = 6.0;
    CHECK_NEAR(normalization_gain_linear(b), std::pow(10.0, 6.0 / 20.0), 0.0001);
    b.normalizationMode = NormalizationMode::Off;
    CHECK_NEAR(normalization_gain_linear(b), 1.0, 0.0001);
}

static void test_corrupt_config_fails_without_generating_replacement() {
    CHECK_THROWS(decode_board_config("{ this is not json"));
    CHECK_THROWS(decode_board_config(""));
    CHECK_THROWS(decode_board_config("{\"grid\": {\"rows\": 2}, \"buttons\": ["));

    CHECK_NOTHROW(decode_board_config("{\"buttons\": []}"));
    CHECK_NOTHROW(decode_board_config("{\"grid\": \"wrong-type\", \"buttons\": \"wrong-type\", \"unknown\": {\"ignored\": true}}"));
    auto wrongTypes = decode_board_config("{\"grid\": \"wrong-type\", \"buttons\": \"wrong-type\", \"master_volume_db\": \"loud\"}");
    CHECK_EQ(wrongTypes.buttons.size(), 9u);
    CHECK_NEAR(wrongTypes.masterVolumeDb, 0.0f, 0.0001f);
}

static void test_filename_sanitization() {
    std::vector<std::string> inputs = {
        "simple.wav", "two words.wav", "a/b\\c.mp3", "../escape.wav", "unicode-☃.ogg", "many***chars.flac", "", "..."
    };
    for (const auto& input : inputs) {
        auto out = sanitize_filename(input);
        CHECK(!out.empty());
        CHECK(out.find('/') == std::string::npos);
        CHECK(out.find('\\') == std::string::npos);
        CHECK(out.find("..") == std::string::npos);
        CHECK(out.front() != '.');
    }
    CHECK_EQ(sanitize_filename("simple-name_01.wav"), "simple-name_01.wav");
    CHECK(sanitize_filename("two words.wav").find("two_words.wav") != std::string::npos);
}

static void test_import_path_validation() {
    CHECK(is_valid_package_path("board.json"));
    CHECK(is_valid_package_path("soundboard.json"));
    CHECK(is_valid_package_path("sounds/example.wav"));
    CHECK(is_valid_sound_entry("sounds/example.wav"));

    for (const auto& bad : {"/abs.wav", "sounds/../x.wav", "sounds\\x.wav", "sounds//x.wav", "sounds/.hidden.wav", "other/file.wav", "sounds/nested/file.wav", "", "sounds/"}) {
        CHECK(!is_valid_package_path(bad));
        CHECK_THROWS(validate_package_path_or_throw(bad));
    }
}

static void test_import_limits() {
    PackageLimits limits;
    limits.maxEntries = 2;
    limits.maxSingleFileBytes = 10;
    limits.maxTotalBytes = 20;
    CHECK_NOTHROW(validate_package_entries_or_throw({{"board.json", 5}, {"sounds/a.wav", 10}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"board.json", 1}, {"sounds/a.wav", 1}, {"sounds/b.wav", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"sounds/a.wav", 11}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"sounds/a.wav", 10}, {"sounds/b.wav", 10}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"evil.txt", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"sounds/a.wav", 1}}, limits));
    CHECK_THROWS(validate_package_entries_or_throw({{"soundboard.json", 1}, {"sounds/a.wav", 1}, {"sounds/a.wav", 1}}, limits));
}

static void test_pad_grid_presenter_labels_and_progress() {
    auto s = populated_board();
    const auto& b = s.buttons[0];
    auto text = pad_button_text(b, true);
    CHECK(text.find("Kick") != std::string::npos);
    CHECK(text.find("Reverse ready") != std::string::npos);
    CHECK(text.find("Speed 0.75x") != std::string::npos);
    CHECK_EQ(effective_pad_color(b, s.groups), "blue");

    PlayingInfo info;
    info.key = 0;
    info.mode = PlaybackMode::Retrigger;
    info.groupName = "Drums";
    info.volume = 0.5f;
    info.duckDb = -6.0f;
    info.speed = 1.25f;
    info.position = 1.0;
    info.trimStart = 0.5;
    info.trimEnd = 2.5;
    auto detail = playing_detail(info, &b, find_group(s.groups, "Drums"));
    CHECK(detail.find("Retrigger") != std::string::npos);
    CHECK(detail.find("Drums") != std::string::npos);
    CHECK(detail.find("ducked") != std::string::npos);
    CHECK_NEAR(playback_progress_fraction(1.0, 3.0, 0.5, 2.5), 0.25, 0.0001);
}

int main() {
    test_round_trip_populated_board();
    test_empty_and_minimal_configs();
    test_invalid_values_default_or_clamp();
    test_playback_config_migration();
    test_normalization_config_and_helpers();
    test_normalization_mode_ui_ids_parse();
    test_normalization_regions_and_invalidation();
    test_normalization_gain_is_peak_limited();
    test_corrupt_config_fails_without_generating_replacement();
    test_filename_sanitization();
    test_import_path_validation();
    test_import_limits();
    test_pad_grid_presenter_labels_and_progress();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All pure logic tests passed\n";
    return 0;
}
