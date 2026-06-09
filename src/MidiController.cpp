#include "MidiController.h"

#include <algorithm>

namespace pulsepad {

std::string midi_note_name(int note) {
    if (note < 0 || note > 127) return "";
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = note / 12 - 1;
    return std::string(names[note % 12]) + std::to_string(octave);
}

std::string format_midi_trigger(int channel, int note) {
    if (channel < 1 || channel > 16 || note < 0 || note > 127) return "None";
    return "Ch " + std::to_string(channel) + " • " + midi_note_name(note) + " (" + std::to_string(note) + ")";
}

std::string format_midi_event(int channel, int note, int velocity) {
    if (!valid_midi_trigger(channel, note)) return "None";
    return format_midi_trigger(channel, note) + "  Vel " + std::to_string(std::max(0, std::min(127, velocity)));
}

bool valid_midi_trigger(int channel, int note) {
    return channel >= 1 && channel <= 16 && note >= 0 && note <= 127;
}

void MidiController::set_note_callback(NoteCallback callback) { callback_ = std::move(callback); }
void MidiController::dispatch_note(MidiNoteEvent event) const { if (callback_) callback_(event); }

} // namespace pulsepad
