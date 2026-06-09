# Building PulsePad GTK

PulsePad GTK v1 is intended for technical users who build from source.

## Supported platforms

Supported and tested target:

* Linux desktop environments with GTK support
* The current application target builds against `gtkmm-4.0` and GStreamer 1.0 as declared in `CMakeLists.txt`.

The project name is PulsePad GTK, but this source tree does not currently build a GTK4 application target. Do not install GTK4-only development packages and expect this tree to configure successfully.

## Build dependencies

Required to build the full desktop application with the default `PULSEPAD_BUILD_APP=ON`:

* C++17 compiler such as `g++` or `clang++`
* CMake 3.16 or newer
* pkg-config
* gtkmm 4 development files (`gtkmm-4.0`)
* GStreamer 1.0 development files (`gstreamer-1.0`)
* libzip development files (`libzip`)
* nlohmann_json 3.2.0 or newer

Optional build dependency:

* RtMidi development files (`rtmidi` / `librtmidi-dev`) enable MIDI input. If RtMidi is absent, the application still builds, but MIDI input is disabled and the Settings dialog reports that MIDI support was not built.

Runtime tools and plugins:

* `ffprobe` from FFmpeg is used for duration detection. Trim setup and waveform setup are limited when it is missing.
* `ffmpeg` is used for waveform generation and reverse-audio generation.
* GStreamer playback requires the plugins needed to decode the files you import and output audio on your system. The exact packages vary by distribution; base/good/bad plugin sets are commonly needed for MP3, OGG/Vorbis, FLAC, M4A/AAC, converters, volume, pan, and audio output sinks.

Logic-only tests can be configured without the GTK application dependencies:

```bash
cmake -S . -B build -DPULSEPAD_BUILD_APP=OFF
cmake --build build
ctest --test-dir build
```

## Debian / Ubuntu dependency example

```bash
sudo apt install \
    build-essential \
    cmake \
    pkg-config \
    libgtkmm-4.0-dev \
    libgstreamer1.0-dev \
    libzip-dev \
    nlohmann-json3-dev \
    librtmidi-dev \
    ffmpeg \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad
```

`librtmidi-dev` is optional if you do not need MIDI input.

## Build from a clean checkout

```bash
git clone https://github.com/rdm375/PulsePadGTK.git
cd PulsePadGTK
mkdir build
cd build
cmake ..
cmake --build .
```

Run the application from the build directory:

```bash
./PulsePadGTK
```

Run tests:

```bash
ctest --output-on-failure
```

## CMake options

* `PULSEPAD_BUILD_APP=ON` builds the desktop application. This is the default and requires the GUI/audio dependencies above.
* `PULSEPAD_BUILD_APP=OFF` builds logic/test targets only.
* `PULSEPAD_ENABLE_WARNINGS=ON` enables the project warning flags. This is the default.
* `PULSEPAD_WARNINGS_AS_ERRORS=OFF` keeps warnings non-fatal. Enable it only for local strict checks.

## No packaging target for v1

The v1 source release is source-build oriented. Packaging, install integration, Flatpak, AppStream, RPM, and DEB work are intentionally out of scope for this readiness pass.
