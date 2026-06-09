# PulsePad GTK Troubleshooting

PulsePad targets technical Linux users who build and run the application from source. Most runtime problems are caused by missing command-line media tools, missing GStreamer plugins, unavailable audio devices, unavailable MIDI devices, or damaged board/config files.

## Missing `ffprobe`

### Symptoms

* Startup/status area reports that `ffprobe` is not found in PATH.
* Duration may be unknown after loading audio.
* Trim controls and waveform setup may be unavailable because PulsePad cannot determine media length.

### Cause

PulsePad calls `ffprobe` to read audio duration. `ffprobe` normally ships with FFmpeg packages, but some distributions package it separately or the application may be launched with a restricted PATH.

### Resolution

Install FFmpeg/ffprobe and restart PulsePad. On Debian/Ubuntu systems this is typically:

```bash
sudo apt install ffmpeg
```

Verify the executable is visible to the same environment used to launch PulsePad:

```bash
command -v ffprobe
ffprobe -version
```

If PulsePad is launched from a desktop file, log out/in or restart the desktop session after changing PATH-related shell configuration.

## Missing `ffmpeg`

### Symptoms

* Startup/status area reports that `ffmpeg` is not found in PATH.
* Waveform generation is unavailable in the Trim tab.
* Reverse playback cannot prepare reversed audio.

### Cause

PulsePad uses `ffmpeg` for waveform sample extraction and reverse-audio generation. These features are disabled when the executable is not available.

### Resolution

Install FFmpeg and restart PulsePad:

```bash
sudo apt install ffmpeg
```

Verify:

```bash
command -v ffmpeg
ffmpeg -version
```

After installation, reload the affected audio or reopen the pad to regenerate waveform data. Retry reverse playback after the tool is available.

## Missing GStreamer Plugins

### Symptoms

* The app opens, but a particular media file will not play.
* Status reports an audio playback or GStreamer error.
* Some formats work while others fail.
* Stereo pan or boosted pad volume may be unavailable if optional GStreamer elements are missing.

### Cause

Playback uses GStreamer `playbin` and the installed plugin set. PulsePad accepts WAV, MP3, OGG, FLAC, and M4A by extension, but actual decoding depends on plugins installed by the distribution. Audio output also depends on a working GStreamer audio sink.

### Resolution

Install the common GStreamer plugin packages for your distribution. On Debian/Ubuntu systems, start with:

```bash
sudo apt install \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly \
  gstreamer1.0-libav
```

Then restart PulsePad and retry playback. If only one format fails, convert the source to a known-good WAV or FLAC and test again.

## Unsupported Media Formats

### Symptoms

* Loading a file fails with an unsupported-format message.
* The pad assignment is not changed.

### Cause

The current import allow-list is extension based and accepts: `wav`, `mp3`, `ogg`, `flac`, and `m4a`. Other extensions are intentionally rejected before copying into the board storage.

### Resolution

Convert the source audio to WAV, MP3, OGG, FLAC, or M4A. WAV or FLAC is recommended for release validation because decoding support is usually straightforward on Linux systems.

## Corrupt or Malformed Media

### Symptoms

* A supported extension imports, but duration is unknown.
* Waveform generation fails.
* Playback fails with a GStreamer error.

### Cause

The file extension is accepted, but the file contents are corrupt, truncated, encrypted/DRM-protected, or require codecs/plugins not available on the system.

### Resolution

Validate the file outside PulsePad:

```bash
ffprobe path/to/file
ffmpeg -v error -i path/to/file -f null -
```

If those commands fail, repair or replace the media. If they succeed but PulsePad playback fails, install missing GStreamer plugins or convert the file to WAV/FLAC and reload the pad.

## Audio Device Problems

### Symptoms

* Playback fails immediately.
* Status reports that the GStreamer playback pipeline or audio sink could not start.
* Other formats/features work, but no audio is heard.

### Cause

GStreamer could not open a usable output sink, the selected/default output device disappeared, the audio server is not running, or another application has exclusive control of the device.

### Resolution

1. Confirm the system can play audio outside PulsePad.
2. Check PipeWire/PulseAudio/ALSA status for your distribution.
3. Close applications that may be holding the audio device exclusively.
4. Reconnect the output device and retry playback.
5. Start PulsePad from a terminal and capture any GStreamer messages for bug reports.

PulsePad should report the failure and clean up the failed playback instance without crashing.

## MIDI Device Problems

### Symptoms

* Settings reports no MIDI input ports.
* MIDI support is disabled.
* A configured MIDI device no longer triggers pads.
* Status reports that a MIDI input could not be opened.

### Cause

MIDI input requires RtMidi at build time and an available hardware or virtual MIDI input at runtime. Devices may also disappear when disconnected or when a virtual MIDI app is closed.

### Resolution

* If MIDI support was not built, install the RtMidi development package and rebuild PulsePad.
* Connect the MIDI device or start a virtual device such as VMPK before opening Settings.
* Reopen Settings and select an available input port.
* If opening fails, confirm the device is connected and not exclusively in use by another application.

When a configured device is unavailable, PulsePad reports the condition and remains usable without MIDI input.

## Corrupt Project Imports

### Symptoms

* Import fails with an invalid-board, unsupported-version, missing-audio, or write-failure message.
* The current board remains loaded.

### Cause

The selected ZIP is not a PulsePad board package, has an unsupported manifest, is missing required `soundboard.json` or `sounds/` entries, contains unsafe paths, exceeds package safety limits, or cannot be written into PulsePad's config directory.

### Resolution

1. Re-export the board from the same PulsePad version if possible.
2. Confirm the archive contains `soundboard.json` and referenced `sounds/` entries.
3. Confirm the PulsePad config directory is writable:

```bash
ls -ld ~/.config/pulsepad-gtk ~/.config/pulsepad-gtk/sounds
```

4. Retry import on a disposable config if you suspect the package is corrupt.

## Corrupt Local Configuration

### Symptoms

* On startup, PulsePad resets to defaults and reports that a corrupt configuration was reset.
* A backup named `corrupt-config-backup.json` appears in the config directory.

### Cause

`board_config_v2.json` could not be parsed or validated.

### Resolution

Inspect the backup if you need to recover settings manually. Otherwise, rebuild the board from the UI or import a known-good board package.
