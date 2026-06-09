#pragma once

#include "AudioEngine.h"
#include "BoardConfig.h"

#include <string>
#include <vector>

namespace pulsepad {

inline constexpr float PAD_VOLUME_MIN_DB = -60.0f;
inline constexpr float PAD_VOLUME_MAX_DB = 12.041199826559248f;

float linear_volume_to_db(float volume);
float db_to_linear_volume(float db);
std::string format_db(float db);
std::string display_group_type(PadGroupType type);
std::string display_group_transition(GroupTransition transition);
std::string color_for_exclusive_group(const std::string& group);
const PadGroup* find_group(const std::vector<PadGroup>& groups, const std::string& name);
PadGroup* find_group(std::vector<PadGroup>& groups, const std::string& name);
std::string effective_pad_color(const SoundButton& button);
std::string effective_pad_color(const SoundButton& button, const std::vector<PadGroup>& groups);
std::string pad_button_text(const SoundButton& button, bool reverseReady);
std::string playing_title(const PlayingInfo& info, const SoundButton* button);
std::string playing_detail(const PlayingInfo& info, const SoundButton* button, const PadGroup* group);
double playback_progress_fraction(double position, double duration, double trimStart, double trimEnd);

} // namespace pulsepad
