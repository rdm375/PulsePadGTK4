# PulsePad GTK Release Validation Checklist

This checklist is the required manual QA process for every v1.0 release candidate. Run it on a clean Linux desktop session with PulsePad built from source. Record `PASS`, `FAIL`, or `N/A` for every item, with notes and exact file/device names where useful.

Release candidate: ____________________
Tester: ____________________
Date: ____________________
Linux distribution / desktop: ____________________
Audio backend/session: ____________________
MIDI device or virtual source: ____________________

## Setup

| Result | Check | Expected result | Notes |
| --- | --- | --- | --- |
| ___ | Start PulsePad from a terminal. | The window opens without crashes. Startup warnings, if any, are readable in the status area or terminal. | |
| ___ | Confirm `ffmpeg` and `ffprobe` are available for the normal pass. | Waveform, duration/trim, and reverse features are enabled. | |
| ___ | Confirm GStreamer playback plugins for common formats are installed. | WAV, MP3, FLAC, OGG, and M4A playback can be tested if the distribution provides the required plugins. | |
| ___ | Start or connect a MIDI input device if MIDI validation is in scope. | The device appears in Settings when RtMidi support was built. | |

## Media Import

PulsePad's file picker accepts these extensions in the current implementation: WAV, MP3, OGG, FLAC, and M4A. Actual decode/playback still depends on installed GStreamer plugins, and duration/waveform analysis depends on `ffprobe`/`ffmpeg`.

| Result | Format | Steps | Expected result | Notes |
| --- | --- | --- | --- | --- |
| ___ | WAV | Load a valid WAV into an empty pad. | Metadata/duration is extracted when `ffprobe` is available; a waveform is generated when `ffmpeg` is available; the pad label updates; playback succeeds. | |
| ___ | MP3 | Load a valid MP3 into an empty pad. | Metadata/duration is extracted; waveform appears; assignment is saved; playback succeeds if GStreamer MP3 support is installed. | |
| ___ | FLAC | Load a valid FLAC into an empty pad. | Metadata/duration is extracted; waveform appears; assignment is saved; playback succeeds if GStreamer FLAC support is installed. | |
| ___ | OGG | Load a valid OGG/Vorbis file into an empty pad. | Metadata/duration is extracted; waveform appears; assignment is saved; playback succeeds if GStreamer OGG/Vorbis support is installed. | |
| ___ | M4A | Load a valid M4A file into an empty pad. | Metadata/duration is extracted; waveform appears; assignment is saved; playback succeeds if GStreamer AAC/MP4 support is installed. | |
| ___ | Unsupported extension | Try to load a file outside the accepted extension set. | Import is rejected with a user-visible unsupported-format message; the existing pad assignment is preserved. | |

## Playback Validation

| Result | Check | Steps | Expected result | Notes |
| --- | --- | --- | --- | --- |
| ___ | Normal playback | Trigger an assigned pad once. | Audio starts promptly; status says playback started; mixer shows the active sound and progress. | |
| ___ | Playback completion | Let the sound finish naturally. | Playback stops; the mixer entry disappears; no crash or stuck active state remains. | |
| ___ | Rapid retriggering | Set playback mode to Retrigger and press the pad rapidly. | Previous playback for that pad stops/restarts cleanly; no crash, runaway overlap, or stuck mixer entry. | |
| ___ | Overlapping playback | Set playback mode to Play-through and press the pad repeatedly. | Multiple instances can overlap; mixer reflects active playback until each instance completes. | |
| ___ | Stop all | Start multiple sounds and press Stop. | All active sounds stop or fade according to current settings; mixer clears. | |
| ___ | Looping | Set playback mode to Loop and trigger a short sound. | The sound loops continuously until stopped. | |
| ___ | Trimmed playback | Set non-default trim start/end and play. | Playback starts and stops at the configured trim range. | |
| ___ | Speed adjustment | Set playback speed above/below 1.0 and play. | Playback speed changes through GStreamer rate seeking. | |
| ___ | Pitch compatibility field | Load/save a board containing non-default `playback_pitch`. | The value is retained in serialized config for compatibility, but runtime playback continues at normal pitch with a status warning. | |

## Group Behavior

| Result | Check | Steps | Expected result | Notes |
| --- | --- | --- | --- | --- |
| ___ | Group creation | Open Settings and create a named group. | The group is saved and appears in pad group selectors. | |
| ___ | Group assignment | Assign two pads to the same group. | Both pads display/use the selected group. | |
| ___ | Same exclusive group | Configure an Exclusive group and trigger one grouped pad, then another. | The first grouped pad stops or fades according to the group transition when the second starts. | |
| ___ | Different groups | Trigger pads in different groups. | Pads in different groups do not stop each other solely because of grouping. | |
| ___ | Crossfade/fade transition | Configure group transition/fade settings and trigger between grouped pads. | The outgoing pad fades and the incoming pad starts/fades according to configured group settings. | |
| ___ | Legacy unknown group behavior | Import or load a board with a non-empty group name not present in group settings. | The group behaves conservatively as exclusive rather than allowing uncontrolled overlap. | |

## Ducking

| Result | Check | Steps | Expected result | Notes |
| --- | --- | --- | --- | --- |
| ___ | Attenuation occurs | Configure a ducking relationship where one group ducks another, then trigger the priority pad. | Active sounds in the ducked group attenuate by the configured dB amount. | |
| ___ | Recovery | Stop or let the priority pad finish. | The ducked group returns to its original level. | |
| ___ | Rapid retrigger edge case | Rapidly retrigger the priority pad while another group is ducked. | Ducking remains bounded, does not stack indefinitely, and recovers after priority playback ends. | |
| ___ | Multiple active pads | Play multiple pads in the ducked group, then trigger the priority pad. | All applicable active pads attenuate consistently. | |

## MIDI

| Result | Check | Steps | Expected result | Notes |
| --- | --- | --- | --- | --- |
| ___ | MIDI support present | Build with RtMidi and open Settings. | MIDI controls are enabled; available input ports are listed. | |
| ___ | MIDI unavailable at build time | Build without RtMidi, if possible. | Status/settings clearly report that MIDI support was not built; the app remains usable. | |
| ___ | MIDI learn | Enable MIDI, open a pad, click Learn MIDI, then send a Note On. | The pad records the channel/note and displays it. | |
| ___ | MIDI trigger | Send the learned Note On. | The mapped pad triggers playback. | |
| ___ | Reconnect behavior | Disconnect/reconnect or stop/start the MIDI source, then reopen Settings and select the port. | Missing devices are reported; reselecting an available port restores input without restarting when possible. | |
| ___ | Unavailable configured device | Configure a MIDI port, close PulsePad, remove the device, then relaunch. | The app does not crash; status explains that no MIDI port was found or that an available port is being used. | |

## Waveforms

| Result | Check | Steps | Expected result | Notes |
| --- | --- | --- | --- | --- |
| ___ | Initial generation | Load a supported audio file and open the Trim tab. | The UI reports generation and then displays a waveform. | |
| ___ | Regeneration | Replace the pad audio or reload a different file. | The previous waveform is cleared and a new waveform is generated. | |
| ___ | Display correctness | Compare waveform shape against audio with obvious silence/peaks. | The display roughly reflects quiet and loud sections. | |
| ___ | Large-file behavior | Load a long file. | The UI remains responsive; waveform generation either completes or reports an actionable failure without crashing. | |
| ___ | Missing ffmpeg behavior | Run without `ffmpeg` in PATH. | The Trim tab reports that waveform generation is unavailable and instructs the user to install ffmpeg. | |

## Export / Import Round Trip

| Result | Check | Steps | Expected result | Notes |
| --- | --- | --- | --- | --- |
| ___ | Export board | Create a board with multiple assigned pads and export it. | A ZIP package is written; failures identify unwritable destinations or missing source audio. | |
| ___ | Re-import board | Import the exported package after confirmation. | Board state is restored from the package. | |
| ___ | Media references preserved | Play every imported assigned pad. | Imported media files are present in PulsePad's storage and playback succeeds. | |
| ___ | Settings preserved | Check labels, colors, groups, hotkeys, MIDI mappings, playback modes, trim, speed, fades, pan, and ducking. | Settings match the exported board. `playback_pitch` is retained for compatibility but ignored at runtime. | |
| ___ | Corrupt package | Attempt to import a malformed or incomplete package. | Import fails with an invalid/missing-files message; current board is not replaced. | |

## Failure Scenarios

Run these on a disposable config or test VM where possible.

| Result | Scenario | Steps | Expected result | Notes |
| --- | --- | --- | --- | --- |
| ___ | Missing ffprobe | Start PulsePad with `ffprobe` unavailable in PATH. | Startup/status reports that `ffprobe` is missing; duration/trim setup may be unavailable; the app does not crash. | |
| ___ | Missing ffmpeg | Start PulsePad with `ffmpeg` unavailable in PATH. | Startup/status reports that `ffmpeg` is missing; waveform and reverse generation are unavailable; the app does not crash. | |
| ___ | Unsupported media | Try to import an unsupported extension. | Import is rejected; the pad remains unchanged. | |
| ___ | Corrupt media | Import or play a corrupt file with a supported extension. | Import may copy the file but duration/waveform/playback reports failure; the app does not crash. | |
| ___ | Missing GStreamer plugins | Test playback for a format whose decode plugin is absent. | Playback fails with a GStreamer error in the status area; the player is cleaned up and the app remains usable. | |
| ___ | Audio device unavailable | Start playback with no usable output device or with the device blocked. | GStreamer sink/pipeline failure is reported; playback does not crash the app. | |
| ___ | MIDI device unavailable | Enable MIDI without an input port. | Status reports no MIDI input ports and suggests connecting or starting a device. | |
| ___ | MIDI open failure | Select a MIDI device that cannot be opened or is disconnected. | Status reports the open failure and suggests checking connection/exclusive use. | |
| ___ | Corrupt local config | Replace `board_config_v2.json` with invalid JSON, then start PulsePad. | PulsePad backs up the corrupt config, resets to defaults, and reports the reset. | |

## Release Decision

| Result | Check | Notes |
| --- | --- | --- |
| ___ | No crash observed during required validation. | |
| ___ | All required supported-format import/playback checks passed on the target distribution. | |
| ___ | All recoverable failures produced actionable diagnostics. | |
| ___ | Export/import round trip preserved board contents. | |
| ___ | Known risks are documented in release notes. | |

Final decision: ___ PASS / ___ FAIL
