# PulsePad GTK

A Linux desktop soundboard and cartwall for triggering audio pads during live playback.

PulsePad GTK is designed for live sound playback, podcast production, theater, streaming, tabletop gaming, radio-style cart playback, and general-purpose audio triggering.

![Platform](https://img.shields.io/badge/platform-Linux-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B17-orange)
![Toolkit](https://img.shields.io/badge/UI-GTKmm-green)
![Audio](https://img.shields.io/badge/audio-GStreamer-red)
![License](https://img.shields.io/badge/license-MIT-green)

---

## Features

### Playback Engine

* Multi-pad audio playback
* Simultaneous playback of multiple sounds
* Independent pad volume control
* Master output volume
* Stereo field control
* Playback speed adjustment
* Reverse playback
* Play-through, Retrigger, and Looping playback modes

### Audio Editing

Built-in waveform trim UI with:

* Waveform visualization
* Trim start/end markers
* Playback preview
* Real-time playhead display

### Mixer & Monitoring

* Live playback monitor
* Active playback list
* Per-sound progress display
* Group visibility
* Real-time status updates

### Playback Groups

Create custom playback groups to control interactions between sounds.

Supported group behaviors:

* Exclusive groups
* Crossfade groups
* Fade groups
* Ducking groups

### Ducking

Automatically reduce the volume of background sounds when priority sounds are triggered.

Examples:

* Music ducks under voice announcements
* Ambient tracks duck under sound effects
* Narration ducks background beds

### MIDI Support

Control pads from MIDI devices.

Supported:

* MIDI Note On
* MIDI Note Off
* External MIDI controllers
* Virtual MIDI devices

### Pad Management

* Custom pad colors
* Custom labels
* Keyboard shortcuts
* Import/export support

---

## Screenshots

*Screenshots coming soon.*

---

## Installation

### Ubuntu / Debian

Build dependencies:

```bash
sudo apt install \
    build-essential \
    cmake \
    pkg-config \
    libgtkmm-4.0-dev \
    libgstreamer1.0-dev \
    libzip-dev \
    nlohmann-json3-dev \
    librtmidi-dev
```

Runtime dependencies:

```bash
sudo apt install \
    ffmpeg \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad
```

`ffmpeg` is used for waveform generation and reverse-audio preparation. `ffprobe` is used for duration detection and trim/waveform availability. GStreamer plugin availability controls playback features such as decoding, panning, amplification, and audio output. MIDI support is built with RtMidi when `librtmidi-dev` is available; at runtime, PulsePad expects a real or virtual MIDI input port such as a hardware controller or VMPK.

Playback speed is supported through GStreamer rate seeks. The legacy `playback_pitch` config field is still loaded and saved for backward compatibility, but independent pitch shifting is not currently supported at runtime; non-default saved values are reported and playback continues at normal pitch.

See [docs/USER_GUIDE.md](docs/USER_GUIDE.md) for loading sounds, trimming audio, reverse playback, board import/export, MIDI setup, external tools, and troubleshooting.

### Build

```bash
git clone https://github.com/rdm375/PulsePadGTK.git
cd PulsePadGTK

mkdir build
cd build

cmake ..
make -j$(nproc)
```

Run:

```bash
./PulsePadGTK
```

---

## Configuration

PulsePad stores user data in:

```text
~/.config/pulsepad-gtk/
```

This includes:

```text
board_config_v2.json
sounds/
```

---

## Import / Export

PulsePad GTK uses the following archive layout:

```text
soundboard.json
sounds/
```

Boards can be:

* Exported to ZIP archives
* Imported from ZIP archives
* Shared between supported PulsePad implementations

---

## MIDI Testing Without Hardware

PulsePad GTK can be tested without a physical MIDI controller.

Install VMPK:

```bash
sudo apt install vmpk
```

Launch:

```bash
QT_QPA_PLATFORM=xcb vmpk
```

Connect the VMPK output to PulsePad GTK and assign MIDI notes to pads.

---

## License

PulsePad GTK is licensed under the [MIT License](LICENSE).

---

## Acknowledgements

Built with:

* GTKmm
* GStreamer
* RtMidi
* nlohmann/json
* libzip


## Testing

Pure logic tests are available through CTest and intentionally avoid GTK windows, GStreamer playback, RtMidi devices, ffmpeg/ffprobe, and audio hardware.

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

If GTK/GStreamer/libzip/nlohmann-json development packages are not installed, CMake skips the `PulsePadGTK` app target and still builds the pure logic test target.
