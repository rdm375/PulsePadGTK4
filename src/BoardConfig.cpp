#include "BoardConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <set>

namespace pulsepad {
namespace fs = std::filesystem;

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
float clamp_playback_speed(float v) { return clampf(v, 0.0f, 2.0f); }
float clamp_pad_volume(float v) { return clampf(v, 0.0f, 4.0f); }
float clamp_pan(float v) { return clampf(v, -1.0f, 1.0f); }
int clamp_fade_ms(int v) { return std::max(0, std::min(10000, v)); }
float clamp_duck_amount_db(float v) { return clampf(std::abs(v), 0.0f, 36.0f); }
double clamp_time_seconds(double v) { return (!std::isfinite(v) || v < 0.0) ? 0.0 : v; }
int clamp_grid_size(int v) { return std::max(1, std::min(8, v)); }

std::string trim_copy(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
    return s;
}

std::string sanitize_filename(const std::string& name) {
    std::string out;
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '_' || c == '-') out.push_back(c);
        else out.push_back('_');
    }
    while (out.find("..") != std::string::npos) out.replace(out.find(".."), 2, "._");
    while (!out.empty() && (out.front() == '.' || out.front() == '/' || out.front() == '\\')) out.erase(out.begin());
    return out.empty() ? "audio" : out;
}

std::string basename_without_ext(const std::string& name) {
    fs::path p(name);
    auto stem = p.stem().string();
    return stem.empty() ? name : stem;
}

const std::vector<std::string>& pad_color_ids() {
    static const std::vector<std::string> ids = {"default", "red", "orange", "yellow", "green", "blue", "purple", "pink", "gray"};
    return ids;
}

std::string normalize_pad_color(const std::string& color) {
    std::string c = color;
    std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    for (const auto& id : pad_color_ids()) if (c == id) return id;
    return "default";
}

std::string display_pad_color(const std::string& color) {
    std::string c = normalize_pad_color(color);
    if (c == "default") return "Default";
    c[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(c[0])));
    return c;
}

std::string normalize_group_name(const std::string& name) { return trim_copy(name); }

std::string to_string(AppThemeMode m) { return m == AppThemeMode::Dark ? "Dark" : "Light"; }
std::string to_string(PlaybackDirection d) { return d == PlaybackDirection::Reverse ? "Reverse" : "Forward"; }
std::string to_string(PlaybackMode m) {
    switch (m) { case PlaybackMode::Loop: return "Loop"; case PlaybackMode::Retrigger: return "Retrigger"; default: return "PlayThrough"; }
}
std::string to_string(PadGroupType type) { return type == PadGroupType::Exclusive ? "exclusive" : "play"; }
std::string to_string(GroupTransition transition) {
    switch (transition) { case GroupTransition::Fade: return "fade"; case GroupTransition::Crossfade: return "crossfade"; default: return "stop"; }
}
std::string display_label(PlaybackMode m) {
    switch (m) { case PlaybackMode::Loop: return "Loop"; case PlaybackMode::Retrigger: return "Retrigger"; default: return "Play Through"; }
}
std::string display_label(PlaybackDirection d) { return d == PlaybackDirection::Reverse ? "Reverse" : "Forward"; }
AppThemeMode theme_from_string(const std::string& s) { return s == "Dark" ? AppThemeMode::Dark : AppThemeMode::Light; }
PlaybackDirection direction_from_string(const std::string& s) { return s == "Reverse" ? PlaybackDirection::Reverse : PlaybackDirection::Forward; }
PlaybackMode playback_from_string(const std::string& s, bool loop_fallback) {
    if (s == "Loop") return PlaybackMode::Loop;
    if (s == "Retrigger") return PlaybackMode::Retrigger;
    if (s == "PlayThrough") return PlaybackMode::PlayThrough;
    return loop_fallback ? PlaybackMode::Loop : PlaybackMode::PlayThrough;
}
PadGroupType group_type_from_string(const std::string& value) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return v == "exclusive" ? PadGroupType::Exclusive : PadGroupType::Play;
}
GroupTransition group_transition_from_string(const std::string& value) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (v == "fade") return GroupTransition::Fade;
    if (v == "crossfade") return GroupTransition::Crossfade;
    return GroupTransition::Stop;
}

std::string default_button_label(int id) { return "Pad " + std::to_string(id + 1); }
SoundButton default_button(int id) {
    SoundButton b;
    b.id = id;
    b.label = default_button_label(id);
    static const std::uint32_t defaultKeys[] = {'1','2','3','q','w','e','a','s','d'};
    if (id >= 0 && id < 9) {
        b.hotkeyKeyval = defaultKeys[id];
        b.hotkeyLabel = std::string(1, static_cast<char>(defaultKeys[id]));
    }
    return b;
}
std::vector<SoundButton> default_buttons(int count) {
    std::vector<SoundButton> v;
    for (int i = 0; i < count; ++i) v.push_back(default_button(i));
    return v;
}
void ensure_button_count(std::vector<SoundButton>& buttons, int desiredCount) {
    desiredCount = std::max(1, desiredCount);
    if (static_cast<int>(buttons.size()) < desiredCount) {
        for (int i = static_cast<int>(buttons.size()); i < desiredCount; ++i) buttons.push_back(default_button(i));
    } else if (static_cast<int>(buttons.size()) > desiredCount) {
        buttons.resize(static_cast<std::vector<SoundButton>::size_type>(desiredCount));
    }
    for (std::vector<SoundButton>::size_type i = 0; i < buttons.size(); ++i) buttons[i].id = static_cast<int>(i);
}
BoardState default_board_state() { BoardState s; s.buttons = default_buttons(); return s; }

static std::string esc(const std::string& s) {
    std::ostringstream out;
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                else out << static_cast<char>(c);
        }
    }
    return out.str();
}

std::string encode_board_config(const BoardState& state) {
    std::ostringstream o;
    o << std::boolalpha << "{\n";
    o << "  \"format\": \"soundboard-config\",\n  \"version\": 3,\n";
    o << "  \"grid\": {\"rows\": " << state.gridRows << ", \"columns\": " << state.gridColumns << "},\n";
    o << "  \"master_volume_db\": " << clampf(state.masterVolumeDb, -60.0f, 0.0f) << ",\n";
    o << "  \"theme_mode\": \"" << to_string(state.themeMode) << "\",\n";
    o << "  \"midi_enabled\": " << (state.midiEnabled ? "true" : "false") << ",\n";
    o << "  \"midi_port_name\": \"" << esc(state.midiPortName) << "\",\n";
    o << "  \"groups\": [";
    bool first = true;
    for (const auto& g : state.groups) {
        if (normalize_group_name(g.name).empty()) continue;
        if (!first) o << ",";
        first = false;
        o << "{\"name\":\"" << esc(normalize_group_name(g.name)) << "\",\"type\":\"" << to_string(g.type) << "\",\"color\":\"" << normalize_pad_color(g.color)
          << "\",\"transition\":\"" << to_string(g.transition) << "\",\"fade_out_ms\":" << clamp_fade_ms(g.fadeOutMs)
          << ",\"fade_in_ms\":" << clamp_fade_ms(g.fadeInMs) << ",\"duck_enabled\":" << (g.duckEnabled ? "true" : "false")
          << ",\"duck_amount_db\":" << clamp_duck_amount_db(g.duckAmountDb) << ",\"duck_attack_ms\":" << clamp_fade_ms(g.duckAttackMs)
          << ",\"duck_release_ms\":" << clamp_fade_ms(g.duckReleaseMs) << ",\"duck_targets\":[";
        bool tf = true; for (const auto& t : g.duckTargets) { if (!tf) o << ","; tf = false; o << "\"" << esc(t) << "\""; }
        o << "]}";
    }
    o << "],\n  \"buttons\": [";
    for (size_t i = 0; i < state.buttons.size(); ++i) {
        const auto& b = state.buttons[i];
        if (i) o << ",";
        o << "{\"id\":" << b.id << ",\"label\":\"" << esc(b.label) << "\",\"original_filename\":\"" << esc(b.originalFilename)
          << "\",\"stored_filename\":\"" << esc(b.storedFilename) << "\",\"relative_path_or_asset_reference\":\"" << esc(b.relativePathOrAssetReference)
          << "\",\"mime_type\":\"" << esc(b.mimeType) << "\",\"color\":\"" << normalize_pad_color(b.color) << "\",\"volume\":" << b.volume
          << ",\"playback_speed\":" << b.playbackSpeed << ",\"playback_pitch\":" << b.playbackPitch << ",\"pan\":" << b.pan
          << ",\"trim_start\":" << b.trimStart << ",\"trim_end\":" << b.trimEnd << ",\"duration_seconds\":" << b.durationSeconds
          << ",\"fade_in_ms\":" << b.fadeInMs << ",\"fade_out_ms\":" << b.fadeOutMs << ",\"exclusive_group\":\"" << esc(b.exclusiveGroup)
          << "\",\"group\":\"" << esc(b.exclusiveGroup) << "\",\"hotkey_keyval\":" << b.hotkeyKeyval << ",\"hotkey_label\":\"" << esc(b.hotkeyLabel)
          << "\",\"midi_channel\":" << b.midiChannel << ",\"midi_note\":" << b.midiNote << ",\"playback_mode\":\"" << to_string(b.playbackMode)
          << "\",\"playback_direction\":\"" << to_string(b.playbackDirection) << "\",\"assigned\":" << (b.assigned ? "true" : "false")
          << ",\"user_renamed\":" << (b.userRenamed ? "true" : "false") << "}";
    }
    o << "]\n}\n";
    return o.str();
}

class JsonParser {
public:
    enum Type { Null, Bool, Num, Str, Obj, Arr };
    struct V { Type type = Null; bool b = false; double n = 0; std::string s; std::map<std::string,V> o; std::vector<V> a; };
    explicit JsonParser(const std::string& text) : t(text) {}
    V parse() { auto v = value(); ws(); if (p != t.size()) throw std::runtime_error("Trailing JSON"); return v; }
private:
    const std::string& t; size_t p = 0;
    void ws(){ while (p < t.size() && std::isspace(static_cast<unsigned char>(t[p]))) ++p; }
    char peek(){ ws(); if (p >= t.size()) throw std::runtime_error("Unexpected end of JSON"); return t[p]; }
    bool eat(char c){ ws(); if (p < t.size() && t[p] == c){ ++p; return true; } return false; }
    V value(){ char c = peek(); if (c == '{') return object(); if (c == '[') return array(); if (c == '"') { V v; v.type=Str; v.s=string(); return v; } if (c == 't' || c == 'f') return boolean(); if (c == 'n') { literal("null"); return {}; } return number(); }
    void literal(const char* lit){ while (*lit) { if (p >= t.size() || t[p++] != *lit++) throw std::runtime_error("Invalid JSON literal"); } }
    V boolean(){ V v; v.type=Bool; if (t.compare(p,4,"true")==0){ p+=4; v.b=true; } else if (t.compare(p,5,"false")==0){ p+=5; v.b=false; } else throw std::runtime_error("Invalid JSON boolean"); return v; }
    V number(){ ws(); size_t start=p; if (p<t.size() && t[p]=='-') ++p; while(p<t.size() && std::isdigit(static_cast<unsigned char>(t[p]))) ++p; if(p<t.size() && t[p]=='.'){ ++p; while(p<t.size() && std::isdigit(static_cast<unsigned char>(t[p]))) ++p; } if(p<t.size() && (t[p]=='e'||t[p]=='E')){ ++p; if(p<t.size()&&(t[p]=='+'||t[p]=='-'))++p; while(p<t.size()&&std::isdigit(static_cast<unsigned char>(t[p])))++p; } V v; v.type=Num; v.n=std::stod(t.substr(start,p-start)); return v; }
    std::string string(){ if(!eat('"')) throw std::runtime_error("Expected string"); std::string out; while(p<t.size()){ char c=t[p++]; if(c=='"') return out; if(c=='\\'){ if(p>=t.size()) throw std::runtime_error("Bad escape"); char e=t[p++]; switch(e){ case '"': out+='"'; break; case '\\': out+='\\'; break; case '/': out+='/'; break; case 'b': out+='\b'; break; case 'f': out+='\f'; break; case 'n': out+='\n'; break; case 'r': out+='\r'; break; case 't': out+='\t'; break; case 'u': out+='?'; p=std::min(t.size(),p+4); break; default: throw std::runtime_error("Bad escape"); } } else out+=c; } throw std::runtime_error("Unterminated string"); }
    V object(){ V v; v.type=Obj; eat('{'); if(eat('}')) return v; do { std::string k=string(); if(!eat(':')) throw std::runtime_error("Expected colon"); v.o[k]=value(); } while(eat(',')); if(!eat('}')) throw std::runtime_error("Expected object end"); return v; }
    V array(){ V v; v.type=Arr; eat('['); if(eat(']')) return v; do { v.a.push_back(value()); } while(eat(',')); if(!eat(']')) throw std::runtime_error("Expected array end"); return v; }
};

using JV = JsonParser::V;
static const JV* obj_get(const JV& v, const std::string& k){ if(v.type != JsonParser::Obj) return nullptr; auto it=v.o.find(k); return it == v.o.end() ? nullptr : &it->second; }
static std::string strv(const JV& v, const std::string& k, const std::string& d){ auto p=obj_get(v,k); return p && p->type==JsonParser::Str ? p->s : d; }
static double numv(const JV& v, const std::string& k, double d){ auto p=obj_get(v,k); return p && p->type==JsonParser::Num ? p->n : d; }
static bool boolv(const JV& v, const std::string& k, bool d){ auto p=obj_get(v,k); return p && p->type==JsonParser::Bool ? p->b : d; }

static std::vector<SoundButton> decode_buttons_value(const JV& arr, int desiredCount) {
    desiredCount = std::max(1, desiredCount);
    auto buttons = default_buttons(desiredCount);
    if (arr.type != JsonParser::Arr) return buttons;
    for (int i = 0; i < desiredCount; ++i) {
        const JV* found = nullptr;
        for (const auto& o : arr.a) if (static_cast<int>(numv(o,"id",-1)) == i) { found = &o; break; }
        if (!found) continue;
        const auto& o = *found;
        SoundButton b = default_button(i);
        b.label = strv(o, "label", default_button_label(i));
        b.originalFilename = strv(o, "original_filename", "");
        b.storedFilename = strv(o, "stored_filename", strv(o, "filename_or_asset_id", ""));
        b.relativePathOrAssetReference = strv(o, "relative_path_or_asset_reference", b.storedFilename.empty() ? "" : "sounds/" + b.storedFilename);
        b.mimeType = strv(o, "mime_type", "");
        b.color = normalize_pad_color(strv(o, "color", "default"));
        b.volume = clamp_pad_volume(static_cast<float>(numv(o, "volume", 1.0)));
        float decodedSpeed = static_cast<float>(numv(o, "playback_speed", 1.0));
        b.playbackDirection = direction_from_string(strv(o, "playback_direction", decodedSpeed < 0.0f ? "Reverse" : "Forward"));
        b.playbackSpeed = clamp_playback_speed(std::abs(decodedSpeed));
        b.playbackPitch = clampf(static_cast<float>(numv(o, "playback_pitch", 1.0)), 0.5f, 2.0f);
        b.pan = clamp_pan(static_cast<float>(numv(o, "pan", 0.0)));
        b.trimStart = clamp_time_seconds(numv(o, "trim_start", 0.0));
        b.trimEnd = clamp_time_seconds(numv(o, "trim_end", 0.0));
        b.durationSeconds = clamp_time_seconds(numv(o, "duration_seconds", 0.0));
        b.fadeInMs = clamp_fade_ms(static_cast<int>(numv(o, "fade_in_ms", 0)));
        b.fadeOutMs = clamp_fade_ms(static_cast<int>(numv(o, "fade_out_ms", 0)));
        b.exclusiveGroup = normalize_group_name(strv(o, "group", strv(o, "exclusive_group", "")));
        b.hotkeyKeyval = static_cast<std::uint32_t>(std::max(0.0, numv(o, "hotkey_keyval", b.hotkeyKeyval)));
        b.hotkeyLabel = strv(o, "hotkey_label", b.hotkeyLabel);
        b.midiChannel = static_cast<int>(numv(o, "midi_channel", -1));
        b.midiNote = static_cast<int>(numv(o, "midi_note", -1));
        if (b.midiChannel < 1 || b.midiChannel > 16 || b.midiNote < 0 || b.midiNote > 127) { b.midiChannel = -1; b.midiNote = -1; }
        if (b.trimEnd > 0.0 && b.trimEnd <= b.trimStart) b.trimEnd = 0.0;
        b.playbackMode = playback_from_string(strv(o, "playback_mode", ""), boolv(o, "loop", false));
        b.assigned = boolv(o, "assigned", false);
        b.userRenamed = boolv(o, "user_renamed", false);
        buttons[static_cast<std::vector<SoundButton>::size_type>(i)] = b;
    }
    return buttons;
}

static std::vector<PadGroup> decode_groups_value(const JV& root, const std::vector<SoundButton>& buttons) {
    std::vector<PadGroup> groups;
    if (auto ga = obj_get(root, "groups"); ga && ga->type == JsonParser::Arr) {
        for (const auto& o : ga->a) {
            PadGroup g;
            g.name = normalize_group_name(strv(o, "name", ""));
            if (g.name.empty()) continue;
            g.type = group_type_from_string(strv(o, "type", "play"));
            g.color = normalize_pad_color(strv(o, "color", "default"));
            g.transition = group_transition_from_string(strv(o, "transition", "stop"));
            g.fadeOutMs = clamp_fade_ms(static_cast<int>(numv(o, "fade_out_ms", 500)));
            g.fadeInMs = clamp_fade_ms(static_cast<int>(numv(o, "fade_in_ms", 500)));
            g.duckEnabled = boolv(o, "duck_enabled", false);
            g.duckAmountDb = clamp_duck_amount_db(static_cast<float>(numv(o, "duck_amount_db", 12.0)));
            g.duckAttackMs = clamp_fade_ms(static_cast<int>(numv(o, "duck_attack_ms", 100)));
            g.duckReleaseMs = clamp_fade_ms(static_cast<int>(numv(o, "duck_release_ms", 750)));
            if (auto targets = obj_get(o, "duck_targets"); targets && targets->type == JsonParser::Arr) for (const auto& target : targets->a) if (target.type == JsonParser::Str) { auto t = normalize_group_name(target.s); if (!t.empty() && t != g.name) g.duckTargets.insert(t); }
            bool exists = false; for (const auto& old : groups) exists = exists || old.name == g.name;
            if (!exists && static_cast<int>(groups.size()) < MAX_PAD_GROUPS) groups.push_back(g);
        }
    }
    for (const auto& b : buttons) {
        auto name = normalize_group_name(b.exclusiveGroup);
        bool exists = name.empty(); for (const auto& g : groups) exists = exists || g.name == name;
        if (!exists && static_cast<int>(groups.size()) < MAX_PAD_GROUPS) { PadGroup g; g.name = name; g.type = PadGroupType::Exclusive; groups.push_back(g); }
    }
    return groups;
}

BoardState decode_board_config(const std::string& text) {
    auto root = JsonParser(text).parse();
    if (auto format = obj_get(root, "format"); format && format->type == JsonParser::Str && format->s != "soundboard-config") throw std::runtime_error("Invalid board package manifest");
    const int version = static_cast<int>(numv(root, "version", 1));
    if (version > 3) throw std::runtime_error("Unsupported version: " + std::to_string(version));
    BoardState state = default_board_state();
    if (auto grid = obj_get(root, "grid")) {
        state.gridRows = clamp_grid_size(static_cast<int>(numv(*grid, "rows", 3)));
        state.gridColumns = clamp_grid_size(static_cast<int>(numv(*grid, "columns", 3)));
    }
    if (auto btn = obj_get(root, "buttons")) state.buttons = decode_buttons_value(*btn, state.gridRows * state.gridColumns);
    ensure_button_count(state.buttons, state.gridRows * state.gridColumns);
    state.groups = decode_groups_value(root, state.buttons);
    state.masterVolumeDb = clampf(static_cast<float>(numv(root, "master_volume_db", 0.0)), -60.0f, 0.0f);
    state.themeMode = theme_from_string(strv(root, "theme_mode", "Light"));
    state.midiEnabled = boolv(root, "midi_enabled", true);
    state.midiPortName = strv(root, "midi_port_name", "");
    return state;
}

static void validate_safe_archive_path(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find("..") != std::string::npos || path.find('\\') != std::string::npos) throw std::runtime_error("Unsafe archive path");
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") throw std::runtime_error("Unsafe archive path");
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

bool is_valid_sound_entry(const std::string& path) {
    try { validate_safe_archive_path(path); } catch (...) { return false; }
    if (path.rfind("sounds/", 0) != 0 || path.size() <= 7 || path.back() == '/') return false;
    std::string fn = path.substr(7);
    return fn.find('/') == std::string::npos && !fn.empty() && fn.front() != '.';
}

bool is_valid_package_path(const std::string& path) {
    try { validate_safe_archive_path(path); } catch (...) { return false; }
    return path == "soundboard.json" || path == "board.json" || is_valid_sound_entry(path);
}

void validate_package_path_or_throw(const std::string& path) {
    validate_safe_archive_path(path);
    if (!is_valid_package_path(path)) throw std::runtime_error("Unsupported file in import package: " + path);
}

void validate_package_entries_or_throw(const std::vector<PackageEntryInfo>& entries, const PackageLimits& limits) {
    if (entries.size() > limits.maxEntries) throw std::runtime_error("Import package has too many files");
    std::uint64_t total = 0;
    bool hasManifest = false;
    std::set<std::string> seen;
    for (const auto& e : entries) {
        if (!seen.insert(e.path).second) throw std::runtime_error("Duplicate file in import package: " + e.path);
        if (!is_valid_package_path(e.path)) throw std::runtime_error("Unsupported file in import package: " + e.path);
        if (e.path == "soundboard.json" || e.path == "board.json") hasManifest = true;
        if (e.path != "soundboard.json" && e.path != "board.json" && e.uncompressedSize > limits.maxSingleFileBytes) throw std::runtime_error("Imported audio file is too large");
        if (e.uncompressedSize > std::numeric_limits<std::uint64_t>::max() - total) throw std::runtime_error("Import package is too large");
        total += e.uncompressedSize;
        if (total > limits.maxTotalBytes) throw std::runtime_error("Import package is too large");
    }
    if (!hasManifest) throw std::runtime_error("Missing manifest");
}

} // namespace pulsepad
