# PulsePad GTK User Guide

PulsePad GTK is a desktop soundboard for assigning audio files to pads and triggering them during live playback.

## First launch

On a fresh configuration, every pad starts empty. Open a pad, choose **Load Audio**, select an audio file, then press the pad to play it. The status area at the bottom reports important setup problems and background work.

## Loading sounds onto pads

1. Click an empty pad.
2. On the **Pad** tab, choose **Load Audio**.
3. Select a supported audio file: WAV, MP3, OGG, FLAC, or M4A.
4. Adjust the label, color, group, hotkey, MIDI mapping, playback mode, volume, speed, pan, and fades as needed.
5. Choose **OK** to save the pad.

If the original audio file is missing or cannot be copied, PulsePad will ask you to load the sound again.

## Trimming audio

Open a pad and select the **Trim** tab.

* When no audio is loaded, the tab explains that you need to load audio first.
* While the waveform is being generated, the tab shows progress text.
* When the waveform is ready, drag the start and end handles to trim.
* Click the waveform to move the playhead.
* Use **Play Preview** to hear the selected trim range.
* Use **Reset** to return to the full audio length.

Trim and waveform features depend on duration detection. If `ffprobe` is missing or the file duration cannot be read, PulsePad will explain that trim is unavailable for that file.

## Reverse playback

Set **Direction** to **Reverse** on the **Playback** tab. PulsePad generates a reversed copy of the source audio in the background the first time it is needed.

* While the reversed copy is being prepared, the pad/status text shows that reverse audio is generating.
* When ready, reverse playback is available from the pad.
* If generation fails, the error is recoverable: try again after fixing the source audio or installing `ffmpeg`.
* If the source audio changes, old reverse output is invalidated and regenerated when needed.

Reverse generation requires `ffmpeg`.

## Importing and exporting boards

Use the toolbar buttons:

* **Import board** loads a PulsePad board package and replaces the current board after confirmation.
* **Export board** saves the current board and its audio files as a package.

PulsePad reports whether import/export is working, completed, cancelled, or failed. Import failures distinguish invalid packages, unsupported versions, missing package files, and write failures. Export failures distinguish unwritable destinations, missing source audio, and packaging failures.

## MIDI setup

PulsePad supports MIDI input when it is built with RtMidi.

1. Connect a hardware MIDI controller or start a virtual MIDI device.
2. Open **Settings** and enable MIDI input.
3. Select the MIDI input port if needed.
4. Open a pad, choose **Learn MIDI**, then press a MIDI note.
5. Trigger the pad from the mapped MIDI note.

If no MIDI device appears, confirm that the device is connected before launching PulsePad, that your system exposes it as a MIDI input port, and that PulsePad was built with RtMidi support.

## External tools

Required/recommended runtime tools:

* `ffmpeg`: waveform generation and reverse audio generation.
* `ffprobe`: duration detection, trim availability, and waveform setup.

PulsePad will still open without these tools, but affected features are disabled and the status area explains what is missing.

## Troubleshooting

### Waveform unavailable

Install `ffmpeg` and `ffprobe`, then reload the audio. If the file is corrupt or unsupported by your installed codecs, try converting it to WAV, FLAC, OGG, MP3, or M4A.

### Reverse generation failed

Check that `ffmpeg` is installed and that the source audio still exists. Reopen the pad or trigger reverse playback again to retry.

### Audio file missing

The stored audio asset is no longer in PulsePad's config directory. Load the sound onto the pad again.

### App cannot import a board

The package may be invalid, from an unsupported future version, missing audio files, or blocked by filesystem permissions. Try importing a package exported by the current PulsePad version and confirm the config directory is writable.

### App cannot export a board

Check that the destination folder is writable and that every assigned pad still has its stored audio file.

### MIDI device not detected

Confirm RtMidi support was enabled at build time, connect or start the MIDI device before opening PulsePad, and verify that your Linux audio/MIDI system exposes the device as an input port.
