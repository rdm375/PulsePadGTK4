#pragma once

#include <functional>
#include <string>
#include <vector>

namespace pulsepad {

struct MidiNoteEvent {
    int channel = -1;
    int note = -1;
    int velocity = 0;
};

std::string midi_note_name(int note);
std::string format_midi_trigger(int channel, int note);
std::string format_midi_event(int channel, int note, int velocity);
bool valid_midi_trigger(int channel, int note);

class MidiController {
public:
    using NoteCallback = std::function<void(MidiNoteEvent)>;

    void set_note_callback(NoteCallback callback);
    void dispatch_note(MidiNoteEvent event) const;

private:
    NoteCallback callback_;
};

} // namespace pulsepad
