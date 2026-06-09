#include "MidiController.h"

#include <cassert>
#include <string>

int main() {
    using namespace pulsepad;

    assert(midi_note_name(60) == "C4");
    assert(midi_note_name(-1).empty());
    assert(format_midi_trigger(1, 60) == "Ch 1 • C4 (60)");
    assert(format_midi_trigger(17, 60) == "None");
    assert(format_midi_event(1, 60, 200) == "Ch 1 • C4 (60)  Vel 127");
    assert(format_midi_event(0, 60, 64) == "None");
    assert(valid_midi_trigger(16, 127));
    assert(!valid_midi_trigger(16, 128));

    bool called = false;
    MidiController controller;
    controller.set_note_callback([&](MidiNoteEvent event) {
        called = true;
        assert(event.channel == 2);
        assert(event.note == 61);
        assert(event.velocity == 42);
    });
    controller.dispatch_note({2, 61, 42});
    assert(called);

    return 0;
}
