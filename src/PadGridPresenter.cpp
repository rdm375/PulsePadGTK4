#include "PadGridPresenter.h"
#include "Waveform.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace pulsepad {

float linear_volume_to_db(float v) {
    v = clamp_pad_volume(v);
    if (v <= 0.000001f) return PAD_VOLUME_MIN_DB;
    return clampf(20.0f * std::log10(v), PAD_VOLUME_MIN_DB, PAD_VOLUME_MAX_DB);
}

float db_to_linear_volume(float db) {
    db = clampf(db, PAD_VOLUME_MIN_DB, PAD_VOLUME_MAX_DB);
    if (db <= PAD_VOLUME_MIN_DB + 0.0001f) return 0.0f;
    return clamp_pad_volume(std::pow(10.0f, db / 20.0f));
}

std::string format_db(float db) {
    if (db <= PAD_VOLUME_MIN_DB + 0.0001f) return "Mute";
    std::ostringstream ss;
    if (db > 0.0f) ss << "+";
    ss << std::fixed << std::setprecision(1) << db << " dB";
    return ss.str();
}

std::string display_group_type(PadGroupType type) {
    return type == PadGroupType::Exclusive ? "Exclusive" : "Play Group";
}

std::string display_group_transition(GroupTransition transition) {
    switch (transition) {
        case GroupTransition::Fade: return "Fade";
        case GroupTransition::Crossfade: return "Crossfade";
        case GroupTransition::Stop:
        default: return "Stop";
    }
}

std::string color_for_exclusive_group(const std::string& group) {
    static const std::vector<std::string> groupColors = {"red", "orange", "yellow", "green", "blue", "purple", "pink", "gray"};
    std::string g = trim_copy(group);
    if (g.empty()) return "default";
    return groupColors[std::hash<std::string>{}(g) % groupColors.size()];
}

PadGroup* find_group(std::vector<PadGroup>& groups, const std::string& name) {
    std::string n = normalize_group_name(name);
    for (auto& g : groups) {
        if (g.name == n) return &g;
    }
    return nullptr;
}

const PadGroup* find_group(const std::vector<PadGroup>& groups, const std::string& name) {
    std::string n = normalize_group_name(name);
    for (const auto& g : groups) {
        if (g.name == n) return &g;
    }
    return nullptr;
}

std::string effective_pad_color(const SoundButton& b) {
    std::string groupColor = color_for_exclusive_group(b.exclusiveGroup);
    if (groupColor != "default") return groupColor;
    return normalize_pad_color(b.color);
}

std::string effective_pad_color(const SoundButton& b, const std::vector<PadGroup>& groups) {
    if (const auto* g = find_group(groups, b.exclusiveGroup)) {
        std::string c = normalize_pad_color(g->color);
        if (c != "default") return c;
        return color_for_exclusive_group(g->name);
    }
    return effective_pad_color(b);
}

std::string pad_button_text(const SoundButton& b, bool reverseReady) {
    std::ostringstream speedText;
    speedText << std::fixed << std::setprecision(2) << b.playbackSpeed;
    std::string text = b.label + "\n";
    if (!b.assigned) text += "Unassigned\n";
    text += "Vol " + format_db(linear_volume_to_db(b.volume)) + "  Speed " + speedText.str() + "x\n";
    text += display_label(b.playbackDirection) + "  " + display_label(b.playbackMode);
    bool isTrimmed = b.trimStart > 0.001;
    if (!isTrimmed && b.assigned && b.durationSeconds > 0.0 && b.trimEnd > 0.0) {
        isTrimmed = std::abs(b.trimEnd - b.durationSeconds) > 0.01;
    }
    if (isTrimmed) text += "\nTrimmed";
    if (b.playbackDirection == PlaybackDirection::Reverse) {
        text += reverseReady ? "\nReverse ready" : "\nReverse pending";
    }
    return text;
}

std::string playing_title(const PlayingInfo& info, const SoundButton* button) {
    if (!button) return "Preview";
    return button->label.empty() ? ("Pad " + std::to_string(info.key + 1)) : button->label;
}

std::string playing_detail(const PlayingInfo& info, const SoundButton* button, const PadGroup* group) {
    const double duration = button ? button->durationSeconds : 0.0;
    const double segmentEnd = info.trimEnd > 0.0 ? info.trimEnd : duration;
    const double segmentStart = info.trimStart;
    const double denom = segmentEnd > segmentStart ? (segmentEnd - segmentStart) : duration;
    const double rel = info.position - segmentStart;

    std::string meta = display_label(info.mode);
    if (!info.groupName.empty()) meta += "  •  " + info.groupName;
    meta += "  •  " + format_db(linear_volume_to_db(info.volume));
    if (info.duckDb < -0.01f) meta += "  •  ducked " + format_db(info.duckDb);
    if (button && group && group->duckEnabled) {
        meta += "  •  ducking source";
        if (group->duckTargets.empty()) meta += " (all)";
    }
    if (std::abs(info.speed - 1.0f) > 0.001f) {
        std::ostringstream sp;
        sp << std::fixed << std::setprecision(2) << info.speed << "x";
        meta += "  •  " + sp.str();
    }
    if (duration > 0.0) meta += "  •  " + format_seconds(std::max(0.0, rel)) + " / " + format_seconds(denom);
    if (info.stopping) meta += "  •  fading out";
    return meta;
}

double playback_progress_fraction(double position, double duration, double trimStart, double trimEnd) {
    const double segmentEnd = trimEnd > 0.0 ? trimEnd : duration;
    const double denom = segmentEnd > trimStart ? (segmentEnd - trimStart) : duration;
    const double rel = position - trimStart;
    return denom > 0.0 ? std::max(0.0, std::min(1.0, rel / denom)) : 0.0;
}

} // namespace pulsepad
