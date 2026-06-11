#include <gtkmm.h>
#include <gtk/gtk.h>
#include <zip.h>
#include "BoardConfig.h"
#include "AudioEngine.h"
#include "BoardPackage.h"
#include "BoardRepository.h"
#include "MidiController.h"
#include "PadGridPresenter.h"
#include "UiDialogs.h"
#include "Subprocess.h"
#include "TaskRunner.h"
#include "Waveform.h"
#include "UserMessages.h"
#include <nlohmann/json.hpp>
#ifdef HAVE_RTMIDI
#include <RtMidi.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

using pulsepad::AppThemeMode;
using pulsepad::AudioEngine;
using pulsepad::BoardState;
using pulsepad::FileDialogMemory;
using pulsepad::GroupTransition;
using pulsepad::PadGroup;
using pulsepad::PadGroupType;
using pulsepad::PlaybackDirection;
using pulsepad::PlaybackMode;
using pulsepad::SoundButton;
using pulsepad::basename_without_ext;
using pulsepad::clamp_duck_amount_db;
using pulsepad::clamp_fade_ms;
using pulsepad::clamp_grid_size;
using pulsepad::clamp_pad_volume;
using pulsepad::clamp_volume_db;
using pulsepad::clamp_pan;
using pulsepad::clamp_playback_speed;
using pulsepad::clamp_time_seconds;
using pulsepad::clampf;
using pulsepad::default_button;
using pulsepad::default_button_label;
using pulsepad::display_pad_color;
using pulsepad::display_label;
using pulsepad::pad_color_ids;
using pulsepad::MAX_PAD_GROUPS;
using pulsepad::format_midi_event;
using pulsepad::format_midi_trigger;
using pulsepad::ensure_button_count;
using pulsepad::group_transition_from_string;
using pulsepad::group_type_from_string;
using pulsepad::generate_waveform_peaks;
using pulsepad::analyze_loudness_with_ffmpeg;
using pulsepad::file_timestamp_for_analysis;
using pulsepad::TaskHandle;
using pulsepad::TaskOutcome;
using pulsepad::TaskRunner;
using pulsepad::TaskStatus;
using pulsepad::normalize_group_name;
using pulsepad::normalize_pad_color;
using pulsepad::normalization_gain_linear;
using pulsepad::NormalizationMode;
using pulsepad::normalization_mode_from_string;
using pulsepad::normalization_analysis_region;
using pulsepad::invalidate_normalization_analysis;
using pulsepad::playback_from_string;
using pulsepad::sanitize_filename;
using pulsepad::theme_from_string;
using pulsepad::to_string;
using pulsepad::trim_copy;
using pulsepad::valid_midi_trigger;
using pulsepad::db_to_linear_volume;
using pulsepad::display_group_transition;
using pulsepad::display_group_type;
using pulsepad::effective_pad_color;
using pulsepad::find_group;
using pulsepad::format_db;
using pulsepad::linear_volume_to_db;
using pulsepad::pad_button_text;
using pulsepad::playing_detail;
using pulsepad::playing_title;
using pulsepad::playback_progress_fraction;
using pulsepad::PAD_VOLUME_MAX_DB;
using pulsepad::PAD_VOLUME_MIN_DB;
namespace ui = pulsepad::ui;
namespace um = pulsepad::user_message;

static std::size_t checked_index(int value) {
    return value < 0 ? std::size_t{0} : static_cast<std::size_t>(value);
}

static void set_margin(Gtk::Widget& widget, int margin) {
    widget.set_margin_top(margin);
    widget.set_margin_bottom(margin);
    widget.set_margin_start(margin);
    widget.set_margin_end(margin);
}

static int run_dialog_blocking(Gtk::Dialog& dialog) {
    int response = Gtk::ResponseType::NONE;
    Glib::RefPtr<Glib::MainLoop> loop = Glib::MainLoop::create(false);
    dialog.signal_response().connect([&](int id) {
        response = id;
        dialog.hide();
        if (loop->is_running()) loop->quit();
    });
    dialog.set_modal(true);
    dialog.present();
    loop->run();
    return response;
}



using BoardRepository = pulsepad::BoardRepository;
using BoardPackage = pulsepad::ZipBoardPackage;

class PulsePadWindow : public Gtk::Window {
public:
    PulsePadWindow() : package(repository), taskRunner(TaskRunner::default_worker_count(), 32, [](std::function<void()> fn) { Glib::signal_idle().connect_once([fn = std::move(fn)]() mutable { fn(); }); }) {
        set_title("PulsePad GTK");
        set_default_size(540, 720);
        state = repository.load();
        fileDialogMemory = repository.load_file_dialog_memory();
        const auto audioCaps = AudioEngine::runtime_capabilities();
        ffmpegAvailable = audioCaps.ffmpegAvailable;
        ffprobeAvailable = audioCaps.ffprobeAvailable;
        panoramaAvailable = audioCaps.panoramaAvailable;
        audioAmplifyAvailable = audioCaps.audioAmplifyAvailable;
        append_dependency_status();
        audio.set_master_volume(db_to_linear_volume(state.masterVolumeDb));
        midiDispatcher.connect(sigc::mem_fun(*this, &PulsePadWindow::process_midi_events));
        build_ui();
        apply_theme();
        refresh_ui();
        open_configured_midi_port();
        refresh_ui();
        
    }

    ~PulsePadWindow() override {
        shuttingDown = true;
        *windowAlive = false;
        close_midi_port();
        audio.stop_all();
        cancel_reverse_jobs();
        taskRunner.request_stop();
    }

private:
    BoardRepository repository;
    AudioEngine audio;
    BoardPackage package;
    BoardState state;
    FileDialogMemory fileDialogMemory;

    Gtk::Box root{Gtk::Orientation::VERTICAL, 12};
    Gtk::Box toolbar{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::ScrolledWindow padScroller;
    Gtk::Grid grid;
    Gtk::Box bottom{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Expander mixerExpander{"Mixer / Currently Playing"};
    Gtk::Box mixerList{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label mixerEmptyLabel{"No sounds playing"};
    std::map<int, Gtk::ProgressBar*> mixerProgressByKey;
    size_t mixerLastPlayingCount = 0;
    std::map<std::string, float> currentDuckMap;
    Gtk::Button importButton;
    Gtk::Button exportButton;
    Gtk::Button settingsButton;
    Gtk::Button stopButton;
    Gtk::Scale masterScale{Gtk::Orientation::HORIZONTAL};
    Gtk::Label masterLabel;
    Gtk::Label statusLabel;
    Glib::RefPtr<Gtk::CssProvider> themeCssProvider;
    std::vector<Gtk::Button*> padButtons;
    int focusedPadId = -1;
    std::optional<SoundButton> copiedPad;
    struct ReverseJob {
        int id = -1;
        std::string storedFilename;
        TaskHandle handle;
    };
    std::mutex reverseJobsMutex;
    std::map<int, ReverseJob> reverseJobs;
    std::map<int, std::string> reverseErrors;
    bool boardImportInProgress = false;
    bool boardExportInProgress = false;
    std::atomic<bool> shuttingDown{false};
    std::shared_ptr<std::atomic<bool>> windowAlive = std::make_shared<std::atomic<bool>>(true);
    TaskRunner taskRunner;
    bool ffmpegAvailable = false;
    bool ffprobeAvailable = false;
    bool panoramaAvailable = false;
    bool audioAmplifyAvailable = false;
    static constexpr int PREVIEW_KEY_BASE = -100000;

    void remember_file_dialog_folder(std::string& slot, const std::string& selectedFile) {
        if (selectedFile.empty()) return;
        std::error_code ec;
        fs::path parent = fs::path(selectedFile).parent_path();
        if (parent.empty() || !fs::exists(parent, ec) || !fs::is_directory(parent, ec)) return;
        slot = parent.string();
        try { repository.save_file_dialog_memory(fileDialogMemory); } catch (const std::exception&) {}
    }

    struct MidiNoteEvent { int channel = -1; int note = -1; int velocity = 0; };
    Glib::Dispatcher midiDispatcher;
    std::mutex midiMutex;
    std::queue<MidiNoteEvent> midiQueue;
    std::function<void(int, int, int)> midiLearnCallback;
#ifdef HAVE_RTMIDI
    std::unique_ptr<RtMidiIn> midiIn;
#endif

    template <typename Fn>
    static void post_to_ui_if_alive(std::shared_ptr<std::atomic<bool>> alive, Fn&& fn) {
        Glib::signal_idle().connect_once([alive, fn = std::forward<Fn>(fn)]() mutable {
            if (!alive || !alive->load()) return;
            fn();
        });
    }

    template <typename Fn>
    void post_to_ui(Fn&& fn) {
        post_to_ui_if_alive(windowAlive, std::forward<Fn>(fn));
    }


    void append_dependency_status() {
        std::vector<std::string> warnings;
        const auto depWarning = um::dependency_warning(ffmpegAvailable, ffprobeAvailable);
        if (!depWarning.empty()) warnings.push_back(depWarning);
        if (!panoramaAvailable) warnings.push_back("GStreamer audiopanorama missing: stereo pan may not work");
        if (!audioAmplifyAvailable) warnings.push_back("GStreamer audioamplify missing: boosted pad/master volume soft clipping may not work");
#ifndef HAVE_RTMIDI
        warnings.push_back("RtMidi missing at build time: MIDI input disabled");
#endif
        if (!warnings.empty()) {
            state.status = warnings.front();
            for (size_t i = 1; i < warnings.size(); ++i) state.status += "; " + warnings[i];
        }
    }

    void cancel_reverse_job(int id) {
        std::lock_guard<std::mutex> lock(reverseJobsMutex);
        auto it = reverseJobs.find(id);
        if (it == reverseJobs.end()) return;
        it->second.handle.cancel();
        reverseJobs.erase(it);
    }

    void cancel_reverse_jobs() {
        std::lock_guard<std::mutex> lock(reverseJobsMutex);
        for (auto& kv : reverseJobs) kv.second.handle.cancel();
        reverseJobs.clear();
    }

    void erase_reverse_job_if_current(int id, const std::string& storedFilename) {
        std::lock_guard<std::mutex> lock(reverseJobsMutex);
        auto it = reverseJobs.find(id);
        if (it != reverseJobs.end() && it->second.storedFilename == storedFilename) reverseJobs.erase(it);
    }

    bool reverse_job_running(int id, const std::string& storedFilename = {}) {
        std::lock_guard<std::mutex> lock(reverseJobsMutex);
        auto it = reverseJobs.find(id);
        if (it == reverseJobs.end()) return false;
        return storedFilename.empty() || it->second.storedFilename == storedFilename;
    }

    std::vector<std::string> midi_port_names() const {
        std::vector<std::string> names;
#ifdef HAVE_RTMIDI
        try {
            RtMidiIn probe;
            unsigned int count = probe.getPortCount();
            for (unsigned int i = 0; i < count; ++i) names.push_back(probe.getPortName(i));
        } catch (...) {}
#endif
        return names;
    }

    static void rtmidi_callback(double, std::vector<unsigned char>* message, void* userData) {
        auto* self = static_cast<PulsePadWindow*>(userData);
        if (!self || !message || message->size() < 3) return;
        unsigned char status = message->at(0);
        unsigned char command = status & 0xF0;
        if (command != 0x90) return;
        int velocity = static_cast<int>(message->at(2));
        if (velocity <= 0) return;
        int channel = static_cast<int>(status & 0x0F) + 1;
        int note = static_cast<int>(message->at(1));
        {
            std::lock_guard<std::mutex> lock(self->midiMutex);
            self->midiQueue.push({channel, note, velocity});
        }
        self->midiDispatcher.emit();
    }

    void close_midi_port() {
#ifdef HAVE_RTMIDI
        if (midiIn) {
            try { midiIn->cancelCallback(); midiIn->closePort(); } catch (...) {}
            midiIn.reset();
        }
#endif
    }

    void open_configured_midi_port() {
#ifdef HAVE_RTMIDI
        close_midi_port();
        if (!state.midiEnabled) return;
        try {
            midiIn = std::make_unique<RtMidiIn>();
            unsigned int count = midiIn->getPortCount();
            if (count == 0) { state.status = "MIDI enabled, but no MIDI input ports found. Connect a MIDI controller or start a virtual MIDI source, then reopen Settings."; return; }
            unsigned int selected = 0;
            if (!state.midiPortName.empty()) {
                for (unsigned int i = 0; i < count; ++i) {
                    if (midiIn->getPortName(i) == state.midiPortName) { selected = i; break; }
                }
            }
            if (!state.midiPortName.empty() && selected == 0 && midiIn->getPortName(0) != state.midiPortName) {
                state.status = "Configured MIDI input was not found; using available port: " + midiIn->getPortName(0);
            }
            state.midiPortName = midiIn->getPortName(selected);
            midiIn->openPort(selected);
            midiIn->ignoreTypes(false, true, true);
            midiIn->setCallback(&PulsePadWindow::rtmidi_callback, this);
        } catch (const std::exception& ex) {
            close_midi_port();
            state.status = std::string("MIDI input could not be opened: ") + ex.what() + ". Check that the device is connected and not exclusively in use, then reopen Settings.";
        }
#else
        if (state.midiEnabled) state.status = "MIDI support not built. Install librtmidi-dev and rebuild.";
#endif
    }

    void process_midi_events() {
        std::queue<MidiNoteEvent> events;
        {
            std::lock_guard<std::mutex> lock(midiMutex);
            std::swap(events, midiQueue);
        }
        while (!events.empty()) {
            auto ev = events.front();
            events.pop();
            state.lastMidiChannel = ev.channel;
            state.lastMidiNote = ev.note;
            state.lastMidiVelocity = ev.velocity;
            state.lastMidiEvent = format_midi_event(ev.channel, ev.note, ev.velocity);
            if (midiLearnCallback) {
                midiLearnCallback(ev.channel, ev.note, ev.velocity);
                continue;
            }
            if (ev.velocity <= 0) continue;
            for (int i = 0; i < static_cast<int>(state.buttons.size()); ++i) {
                const auto& b = state.buttons[checked_index(i)];
                if (valid_midi_trigger(b.midiChannel, b.midiNote) && b.midiChannel == ev.channel && b.midiNote == ev.note) {
                    activate_if_exists(i);
                    break;
                }
            }
        }
    }

    void build_ui() {
        set_child(root);
        ::set_margin(root, 16);
        root.get_style_context()->add_class("pulsepad-root");
        toolbar.get_style_context()->add_class("pulsepad-toolbar");
        bottom.get_style_context()->add_class("pulsepad-bottom");
        statusLabel.get_style_context()->add_class("status-card");

	ui::setup_icon_button(importButton, "document-open-symbolic", "Import board");
	ui::setup_icon_button(exportButton, "document-save-symbolic", "Export board");
        ui::setup_icon_button(settingsButton, "emblem-system-symbolic", "Settings");
        importButton.set_tooltip_text("Import a saved PulsePad board package");
        exportButton.set_tooltip_text("Export this board and its sounds");
        settingsButton.set_tooltip_text("Open app, grid, group, and MIDI settings");
        toolbar.append(importButton );
        toolbar.append(exportButton );
        toolbar.append(settingsButton );

        importButton.signal_clicked().connect(sigc::mem_fun(*this, &PulsePadWindow::on_import_board));
        exportButton.signal_clicked().connect(sigc::mem_fun(*this, &PulsePadWindow::on_export_board));
        settingsButton.signal_clicked().connect(sigc::mem_fun(*this, &PulsePadWindow::open_settings_dialog));


        ui::setup_icon_button(stopButton, "media-playback-stop", "Stop all");
        stopButton.set_tooltip_text("Stop all currently playing pads");
        stopButton.signal_clicked().connect([this]() { audio.stop_all_with_fade(); set_status("All playback stopped"); refresh_mixer_ui(); });
        auto* volumeBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        volumeBox->set_hexpand(true);
        masterScale.set_hexpand(true);
        masterScale.set_range(PAD_VOLUME_MIN_DB, PAD_VOLUME_MAX_DB);
        masterScale.set_increments(1.0, 6.0);
        masterScale.add_mark(PAD_VOLUME_MIN_DB, Gtk::PositionType::BOTTOM, "Mute");
        masterScale.add_mark(-24.0, Gtk::PositionType::BOTTOM, "-24");
        masterScale.add_mark(-12.0, Gtk::PositionType::BOTTOM, "-12");
        masterScale.add_mark(-6.0, Gtk::PositionType::BOTTOM, "-6");
		masterScale.add_mark(-3.0, Gtk::PositionType::BOTTOM, "-3");
        masterScale.add_mark(0.0, Gtk::PositionType::BOTTOM, "0 dB");
		masterScale.add_mark(3.0, Gtk::PositionType::BOTTOM, "+3");
        masterScale.add_mark(6.0, Gtk::PositionType::BOTTOM, "+6");
        masterScale.add_mark(PAD_VOLUME_MAX_DB, Gtk::PositionType::BOTTOM, "+12");
        masterScale.signal_value_changed().connect([this]() { set_master_volume_db(static_cast<float>(masterScale.get_value())); });
        volumeBox->append(masterLabel );
        volumeBox->append(masterScale );
        toolbar.append(stopButton );
        toolbar.append(*volumeBox );
        root.append(toolbar );

        padScroller.set_hexpand(true);
        padScroller.set_vexpand(true);
        padScroller.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        padScroller.set_child(grid);
        root.append(padScroller );

        grid.set_hexpand(true);
        grid.set_vexpand(false);
        grid.set_row_spacing(10);
        grid.set_column_spacing(10);
        rebuild_pad_grid();

        mixerExpander.set_expanded(false);
        mixerEmptyLabel.set_xalign(0.0f);
        mixerList.append(mixerEmptyLabel );
        mixerExpander.set_child(mixerList);
        mixerExpander.get_style_context()->add_class("mixer-panel");
        root.append(mixerExpander );

        statusLabel.set_xalign(0.0f);
        statusLabel.set_wrap(true);
        root.append(statusLabel );
        auto keyController = Gtk::EventControllerKey::create();
        keyController->signal_key_pressed().connect([this](guint keyval, guint, Gdk::ModifierType modifiers) { return on_key_press(keyval, modifiers); }, false);
        add_controller(keyController);
    }

    bool activate_if_exists(int id) {
        if (id < 0 || id >= static_cast<int>(state.buttons.size())) return false;
        primary_activate_button(id);
        return true;
    }

    bool on_key_press(guint keyval, Gdk::ModifierType modifiers) {
        const guint key = gdk_keyval_to_lower(keyval);
        const bool ctrl = (static_cast<unsigned>(modifiers) & static_cast<unsigned>(Gdk::ModifierType::CONTROL_MASK)) != 0;
        if (ctrl && key == 'c') return copy_focused_pad();
        if (ctrl && key == 'v') return paste_to_focused_pad();

        for (int i = 0; i < static_cast<int>(state.buttons.size()); ++i) {
            guint padKey = state.buttons[checked_index(i)].hotkeyKeyval ? gdk_keyval_to_lower(state.buttons[checked_index(i)].hotkeyKeyval) : 0;
            if (padKey && padKey == key) return activate_if_exists(i);
        }
        return false;
    }

    bool effective_dark_theme() const {
        return state.themeMode == AppThemeMode::Dark;
    }

    void apply_theme() {
        const bool dark = effective_dark_theme();

        if (auto settings = Gtk::Settings::get_default()) {
            settings->property_gtk_application_prefer_dark_theme() = dark;
        }

        if (!themeCssProvider) {
            themeCssProvider = Gtk::CssProvider::create();
            Gtk::StyleContext::add_provider_for_display(
                Gdk::Display::get_default(),
                themeCssProvider,
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
            );
        }

        const std::string css = dark ? R"CSS(
.pulsepad-root { background: #202124; color: #f1f3f4; }
button {
 background-image: none;
 border-radius: 18px;
 padding: 10px;
 }
button:focus {
  border-radius: 18px;
  transition: none;
}
button { background-color: #303134;
 color: #f1f3f4;
 border: 1px solid #5f6368;
 }
button:hover { background-color: #3c4043; }
.pad-button {
  background-color: #3f5f7f;
  color: #ffffff;
  font-size: 16px;
  font-weight: 600;
  border-radius: 18px;
  border-width: 4px;
  border-style: solid;
}
.pad-button:focus {
  border-radius: 18px;
}

.pad-default { border-color: #5f6368; }
.pad-red { border-color: #c5221f; }
.pad-orange { border-color: #e8710a; }
.pad-yellow { border-color: #fbbc04; }
.pad-green { border-color: #188038; }
.pad-blue { border-color: #1a73e8; }
.pad-purple { border-color: #9334e6; }
.pad-pink { border-color: #d01884; }
.pad-gray { border-color: #9aa0a6; }
.pad-button.pad-playing { color: #ffffff; }
.pad-button.pad-default.pad-playing { background-color: #4b535a; box-shadow: 0 0 16px 4px rgba(154,160,166,0.55); }
.pad-button.pad-red.pad-playing { background-color: #7b2a27; box-shadow: 0 0 16px 4px rgba(197,34,31,0.65); }
.pad-button.pad-orange.pad-playing { background-color: #7a431b; box-shadow: 0 0 16px 4px rgba(232,113,10,0.65); }
.pad-button.pad-yellow.pad-playing { background-color: #6f5a19; box-shadow: 0 0 16px 4px rgba(251,188,4,0.65); }
.pad-button.pad-green.pad-playing { background-color: #225b34; box-shadow: 0 0 16px 4px rgba(24,128,56,0.65); }
.pad-button.pad-blue.pad-playing { background-color: #244f87; box-shadow: 0 0 16px 4px rgba(26,115,232,0.65); }
.pad-button.pad-purple.pad-playing { background-color: #57307f; box-shadow: 0 0 16px 4px rgba(147,52,230,0.65); }
.pad-button.pad-pink.pad-playing { background-color: #772b5d; box-shadow: 0 0 16px 4px rgba(208,24,132,0.65); }
.pad-button.pad-gray.pad-playing { background-color: #4b535a; box-shadow: 0 0 16px 4px alpha(#9aa0a6, 0.65); }
.status-card { background: #303134; color: #f1f3f4; border-radius: 14px; padding: 12px; }
.mixer-panel { background: #303134; color: #f1f3f4; border-radius: 14px; padding: 8px; }
.mixer-row { padding: 4px; }
scale trough { background: #5f6368; }
scale highlight { background: #8ab4f8; }
entry { background: #303134; color: #f1f3f4; }
)CSS" : R"CSS(
.pulsepad-root { background: #fafafa; color: #202124; }
button { background-image: none; border-radius: 18px; padding: 10px; }
button { background-color: #e8eaed; color: #202124; border: 1px solid #dadce0; }
button:hover { background-color: #dadce0; }
.pad-button {
  background-color: #d2e3fc;
  color: #174ea6;
  font-size: 16px;
  font-weight: 600;
  border-radius: 18px;
  border-width: 4px;
  border-style: solid;
}
.pad-button:focus {
  border-radius: 18px;
}

.pad-default { border-color: #dadce0; }
.pad-red { border-color: #c5221f; }
.pad-orange { border-color: #e8710a; }
.pad-yellow { border-color: #fbbc04; }
.pad-green { border-color: #188038; }
.pad-blue { border-color: #1a73e8; }
.pad-purple { border-color: #9334e6; }
.pad-pink { border-color: #d01884; }
.pad-gray { border-color: #5f6368; }
.pad-button.pad-playing { color: #202124; }
.pad-button.pad-default.pad-playing { background-color: #eef3f8; box-shadow: 0 0 16px 4px rgba(95,99,104,0.35); }
.pad-button.pad-red.pad-playing { background-color: #fce8e6; box-shadow: 0 0 16px 4px rgba(197,34,31,0.45); }
.pad-button.pad-orange.pad-playing { background-color: #feefe3; box-shadow: 0 0 16px 4px rgba(232,113,10,0.45); }
.pad-button.pad-yellow.pad-playing { background-color: #fef7e0; box-shadow: 0 0 16px 4px rgba(251,188,4,0.45); }
.pad-button.pad-green.pad-playing { background-color: #e6f4ea; box-shadow: 0 0 16px 4px rgba(24,128,56,0.45); }
.pad-button.pad-blue.pad-playing { background-color: #e8f0fe; box-shadow: 0 0 16px 4px rgba(26,115,232,0.45); }
.pad-button.pad-purple.pad-playing { background-color: #f3e8fd; box-shadow: 0 0 16px 4px rgba(147,52,230,0.45); }
.pad-button.pad-pink.pad-playing { background-color: #fde7f3; box-shadow: 0 0 16px 4px rgba(208,24,132,0.45); }
.pad-button.pad-gray.pad-playing { background-color: #f1f3f4; box-shadow: 0 0 16px 4px rgba(95,99,104,0.45); }
.status-card { background: #e8eaed; color: #202124; border-radius: 14px; padding: 12px; }
.mixer-panel { background: #e8eaed; color: #202124; border-radius: 14px; padding: 8px; }
.mixer-row { padding: 4px; }
scale trough { background: #dadce0; }
scale highlight { background: #1a73e8; }
entry { background: #ffffff; color: #202124; }
)CSS";

        themeCssProvider->load_from_data(css);
    }

    void update_ducking_state() {
        std::map<std::string, float> next;
        int fadeMs = 750;
        auto playing = audio.currently_playing();
        for (const auto& info : playing) {
            if (info.key < 0 || info.stopping || info.groupName.empty()) continue;
            const auto* source = find_group(state.groups, info.groupName);
            if (!source || !source->duckEnabled) continue;
            fadeMs = std::min(fadeMs, clamp_fade_ms(source->duckAttackMs));
            std::set<std::string> targets = source->duckTargets;
            // Treat an empty target list as "all other groups". This makes the
            // Duck checkbox useful by itself and avoids the confusing case where
            // a group is marked as a ducking source but nothing is attenuated.
            if (targets.empty()) {
                for (const auto& g : state.groups) {
                    if (g.name != source->name) targets.insert(g.name);
                }
            }
            for (const auto& targetName : targets) {
                const auto* target = find_group(state.groups, targetName);
                if (!target || targetName == source->name) continue;
                float targetDuck = -clamp_duck_amount_db(source->duckAmountDb);
                auto it = next.find(targetName);
                if (it == next.end() || targetDuck < it->second) next[targetName] = targetDuck;
            }
        }
        if (next == currentDuckMap) return;
        bool releasing = next.size() < currentDuckMap.size();
        if (releasing) {
            for (const auto& item : currentDuckMap) {
                if (next.find(item.first) == next.end()) {
                    for (const auto& g : state.groups) {
                        if (g.duckTargets.count(item.first)) fadeMs = std::max(fadeMs, clamp_fade_ms(g.duckReleaseMs));
                    }
                }
            }
        }
        currentDuckMap = next;
        audio.set_group_ducks(currentDuckMap, fadeMs);
    }

    void refresh_mixer_ui() {
        update_ducking_state();
        for (auto* child = mixerList.get_first_child(); child;) {
            auto* next = child->get_next_sibling();
            if (child != &mixerEmptyLabel) mixerList.remove(*child);
            child = next;
        }
        mixerProgressByKey.clear();

        auto playing = audio.currently_playing();
        mixerLastPlayingCount = playing.size();
        mixerExpander.set_label("Mixer / Currently Playing (" + std::to_string(playing.size()) + ")");
        if (playing.empty()) {
            mixerEmptyLabel.show();
            return;
        }
        mixerEmptyLabel.hide();

        for (const auto& info : playing) {
            int key = info.key;
            bool padKey = key >= 0 && key < static_cast<int>(state.buttons.size());
            const SoundButton* bp = padKey ? &state.buttons.at(checked_index(key)) : nullptr;
            auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            row->get_style_context()->add_class("mixer-row");
            row->set_hexpand(true);

            const std::string name = playing_title(info, bp);
            const auto* group = bp ? find_group(state.groups, bp->exclusiveGroup) : nullptr;
            const double duration = bp ? bp->durationSeconds : 0.0;
            const double frac = playback_progress_fraction(info.position, duration, info.trimStart, info.trimEnd);
            const std::string meta = playing_detail(info, bp, group);

            auto* textBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
            auto* title = Gtk::make_managed<Gtk::Label>(name);
            title->set_xalign(0.0f);
            auto* detail = Gtk::make_managed<Gtk::Label>(meta);
            detail->set_xalign(0.0f);
            detail->get_style_context()->add_class("dim-label");
            auto* progress = Gtk::make_managed<Gtk::ProgressBar>();
            progress->set_fraction(frac);
            // Keep a lightweight pointer so the polling timer can update progress
            // without rebuilding the whole mixer widget tree every 500 ms.
            // Rebuilding continuously caused RSS to climb during long loops because
            // GTK/GLib retains allocator pages even after short-lived widgets are freed.
            mixerProgressByKey[info.key] = progress;
            textBox->append(*title );
            textBox->append(*detail );
            textBox->append(*progress );

            auto* stop = Gtk::make_managed<Gtk::Button>("Stop");
            stop->signal_clicked().connect([this, key = info.key]() {
                audio.stop_key_immediate(key);
                set_status_light(key >= 0 ? ("Stopped pad " + std::to_string(key + 1)) : "Stopped preview");
                refresh_pad_grid_ui();
                refresh_mixer_ui();
            });

            row->append(*textBox );
            row->append(*stop );
            mixerList.append(*row );
        }
    }


    void update_mixer_progress_ui() {
        update_ducking_state();
        auto playing = audio.currently_playing();
        mixerExpander.set_label("Mixer / Currently Playing (" + std::to_string(playing.size()) + ")");

        // Rebuild only when the set/count changes.  For steady playback, update
        // existing progress bars in place to avoid creating and destroying GTK
        // widgets on every timer tick.
        if (playing.size() != mixerLastPlayingCount) {
            refresh_mixer_ui();
            return;
        }

        for (const auto& info : playing) {
            auto it = mixerProgressByKey.find(info.key);
            if (it == mixerProgressByKey.end() || !it->second) {
                refresh_mixer_ui();
                return;
            }
            int key = info.key;
            bool padKey = key >= 0 && key < static_cast<int>(state.buttons.size());
            const SoundButton* bp = padKey ? &state.buttons.at(checked_index(key)) : nullptr;
            const double duration = bp ? bp->durationSeconds : 0.0;
            it->second->set_fraction(playback_progress_fraction(info.position, duration, info.trimStart, info.trimEnd));
        }
    }

    void refresh_pad_grid_ui() {
        ensure_button_count(state.buttons, state.gridRows * state.gridColumns);
        if (static_cast<int>(padButtons.size()) != state.gridRows * state.gridColumns) rebuild_pad_grid();
        for (int i = 0; i < static_cast<int>(state.buttons.size()) && i < static_cast<int>(padButtons.size()); ++i) {
            const auto& b = state.buttons[checked_index(i)];
            const bool reverseReady = repository.reverse_audio_is_ready(b);
            std::string text = pad_button_text(b, reverseReady);
            const bool reverseRunning = reverse_job_running(i, b.storedFilename);
            auto reverseError = reverseErrors.find(i);
            if (!reverseReady && reverseRunning) {
                text += "\nReversing...";
            } else if (!reverseReady && reverseError != reverseErrors.end() && !reverseError->second.empty()) {
                text += "\nReverse failed";
            }
            auto style = padButtons[checked_index(i)]->get_style_context();
            for (const auto& colorId : pad_color_ids()) style->remove_class("pad-" + colorId);
            style->add_class("pad-" + effective_pad_color(b, state.groups));
            if (audio.is_key_playing(b.id)) style->add_class("pad-playing");
            else style->remove_class("pad-playing");
            padButtons[checked_index(i)]->set_label(text);
        }
    }

    void refresh_header_ui() {
        masterScale.set_value(state.masterVolumeDb);
        masterLabel.set_text("Master volume: " + format_db(state.masterVolumeDb));
        ui::setup_icon_button(settingsButton, "emblem-system-symbolic", "Settings");
        const bool boardBusy = boardImportInProgress || boardExportInProgress;
        importButton.set_sensitive(!boardBusy);
        exportButton.set_sensitive(!boardBusy);
        importButton.set_tooltip_text(boardImportInProgress ? "Importing board..." : "Import a saved PulsePad board package");
        exportButton.set_tooltip_text(boardExportInProgress ? "Exporting board..." : "Export this board and its sounds");
        statusLabel.set_text(state.status);
    }

    void refresh_ui() {
        refresh_pad_grid_ui();
        refresh_header_ui();
        refresh_mixer_ui();
    }

    void persist_and_refresh(const BoardState& newState) {
        state = newState;
        repository.save(state);
        refresh_ui();
    }

    void set_status(const std::string& status) {
        state.status = status;
        refresh_ui();
    }

    void set_status_light(const std::string& status) {
        state.status = status;
        statusLabel.set_text(state.status);
    }

    void set_master_volume_db(float value) {
        state.masterVolumeDb = clamp_volume_db(value);
        audio.set_master_volume(db_to_linear_volume(state.masterVolumeDb));
        repository.save(state);
        refresh_ui();
    }

    void import_audio_path_to_pad(int id, const std::string& selectedFile, const std::string& busyStatus, const std::string& doneStatus) {
        if (id < 0 || id >= static_cast<int>(state.buttons.size())) return;
        cancel_reverse_job(id);
        remember_file_dialog_folder(fileDialogMemory.lastAudioImportDir, selectedFile);
        SoundButton draft = state.buttons.at(checked_index(id));
        SoundButton previous = draft;
        set_status(busyStatus);
        const auto rootDir = repository.root_dir();
        auto alive = windowAlive;
        taskRunner.submit(
            "audio-import",
            [rootDir, selectedFile, draft](pulsepad::CancellationToken token) {
                if (token.cancellation_requested()) return draft;
                BoardRepository workerRepository(rootDir);
                return workerRepository.import_audio_for_button(draft, selectedFile);
            },
            [this, alive, rootDir, id, previous, doneStatus](TaskOutcome<SoundButton> outcome) mutable {
                if (!alive->load()) return;
                if (outcome.status == TaskStatus::Cancelled) {
                    set_status("Audio import cancelled");
                    return;
                }
                if (!outcome.succeeded()) {
                    set_status(outcome.userMessage);
                    return;
                }
                SoundButton imported = std::move(outcome.value);
                if (id < 0 || id >= static_cast<int>(state.buttons.size())) {
                    BoardRepository(rootDir).remove_audio_assets(imported);
                    return;
                }
                imported.id = id;
                state.buttons[checked_index(id)] = imported;
                if (previous.storedFilename != imported.storedFilename) BoardRepository(rootDir).remove_audio_assets(previous);
                reverseErrors.erase(id);
                repository.save(state);
                refresh_ui();
                set_status(doneStatus);
                if (imported.playbackDirection == PlaybackDirection::Reverse) ensure_reverse_ready_or_start(id);
            });
    }

    static std::optional<std::string> first_local_file_from_uri_list(const std::string& text) {
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            if (line.rfind("file://", 0) != 0) return std::nullopt;

            GError* error = nullptr;
            char* filename = g_filename_from_uri(line.c_str(), nullptr, &error);
            if (!filename) {
                if (error) g_error_free(error);
                return std::nullopt;
            }
            std::string path(filename);
            g_free(filename);
            return path;
        }
        return std::nullopt;
    }

    bool import_dropped_audio_path(int id, const std::string& path) {
        if (path.empty()) {
            set_status("Only local audio files can be dropped onto pads");
            return false;
        }
        import_audio_path_to_pad(id, path, "Importing dropped audio...", "Dropped audio assigned to pad");
        return true;
    }

    struct PadDropContext {
        PulsePadWindow* self{};
        int id{};
    };

    static std::optional<std::string> local_path_from_gfile(GFile* file) {
        if (!file) return std::nullopt;
        char* rawPath = g_file_get_path(file);
        if (!rawPath) return std::nullopt;
        std::string path(rawPath);
        g_free(rawPath);
        if (path.empty()) return std::nullopt;
        return path;
    }

    static std::optional<std::string> local_path_from_drop_value(const GValue* value) {
        if (!value) return std::nullopt;

        if (G_VALUE_HOLDS(value, G_TYPE_FILE)) {
            return local_path_from_gfile(G_FILE(g_value_get_object(value)));
        }

#ifdef GDK_TYPE_FILE_LIST
        if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
            auto* fileList = static_cast<GdkFileList*>(g_value_get_boxed(value));
            if (!fileList) return std::nullopt;
            GSList* files = gdk_file_list_get_files(fileList);
            std::optional<std::string> path;
            if (files && files->data) path = local_path_from_gfile(G_FILE(files->data));
            g_slist_free(files);
            return path;
        }
#endif

        if (G_VALUE_HOLDS(value, G_TYPE_STRING)) {
            const char* text = g_value_get_string(value);
            if (!text) return std::nullopt;
            return first_local_file_from_uri_list(text);
        }

        return std::nullopt;
    }

    static gboolean on_pad_file_drop(GtkDropTarget*, const GValue* value, double, double, gpointer userData) {
        auto* context = static_cast<PadDropContext*>(userData);
        if (!context || !context->self) return FALSE;

        const auto path = local_path_from_drop_value(value);
        if (!path || path->empty()) {
            context->self->set_status("Only local audio files can be dropped onto pads");
            return FALSE;
        }

        context->self->focusedPadId = context->id;
        return context->self->import_dropped_audio_path(context->id, *path) ? TRUE : FALSE;
    }

    static void destroy_pad_drop_context(gpointer userData, GClosure*) {
        delete static_cast<PadDropContext*>(userData);
    }

    void add_audio_drop_target(Gtk::Widget& widget, int id) {
        auto* target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY);
        std::vector<GType> acceptedTypes;
        acceptedTypes.push_back(G_TYPE_FILE);
#ifdef GDK_TYPE_FILE_LIST
        acceptedTypes.push_back(GDK_TYPE_FILE_LIST);
#endif
        acceptedTypes.push_back(G_TYPE_STRING);
        gtk_drop_target_set_gtypes(target, acceptedTypes.data(), static_cast<gsize>(acceptedTypes.size()));

        auto* context = new PadDropContext{this, id};
        g_signal_connect_data(target,
                              "drop",
                              G_CALLBACK(&PulsePadWindow::on_pad_file_drop),
                              context,
                              &PulsePadWindow::destroy_pad_drop_context,
                              static_cast<GConnectFlags>(0));
        gtk_widget_add_controller(widget.gobj(), GTK_EVENT_CONTROLLER(target));
    }

    bool copy_focused_pad() {
        if (focusedPadId < 0 || focusedPadId >= static_cast<int>(state.buttons.size())) {
            set_status("Focus a pad before copying");
            return true;
        }
        copiedPad = state.buttons.at(checked_index(focusedPadId));
        set_status("Copied pad " + std::to_string(focusedPadId + 1));
        return true;
    }

    bool paste_to_focused_pad() {
        if (!copiedPad) {
            set_status("No pad copied yet");
            return true;
        }
        if (focusedPadId < 0 || focusedPadId >= static_cast<int>(state.buttons.size())) {
            set_status("Focus a destination pad before pasting");
            return true;
        }
        const int targetId = focusedPadId;
        const SoundButton previous = state.buttons.at(checked_index(targetId));
        try {
            SoundButton pasted = repository.duplicate_button_audio_for_pad(*copiedPad, targetId);
            state.buttons[checked_index(targetId)] = pasted;
            if (previous.storedFilename != pasted.storedFilename) repository.remove_audio_assets(previous);
            cancel_reverse_job(targetId);
            reverseErrors.erase(targetId);
            repository.save(state);
            refresh_ui();
            set_status("Pasted pad " + std::to_string(copiedPad->id + 1) + " to pad " + std::to_string(targetId + 1));
            if (pasted.assigned && pasted.playbackDirection == PlaybackDirection::Reverse) ensure_reverse_ready_or_start(targetId);
        } catch (const std::exception& ex) {
            set_status(ex.what());
        }
        return true;
    }


    void toggle_theme_mode() {
        state.themeMode = (state.themeMode == AppThemeMode::Light) ? AppThemeMode::Dark : AppThemeMode::Light;
        state.status = "Theme: " + to_string(state.themeMode);
        repository.save(state);
        apply_theme();
        refresh_ui();
    }

    void rebuild_pad_grid() {
        while (auto* child = grid.get_first_child()) grid.remove(*child);
        padButtons.clear();
        ensure_button_count(state.buttons, state.gridRows * state.gridColumns);
        for (int i = 0; i < static_cast<int>(state.buttons.size()); ++i) {
            auto* btn = Gtk::make_managed<Gtk::Button>();
            btn->set_hexpand(true);
            btn->set_vexpand(true);
            btn->set_size_request(130, 130);
            btn->get_style_context()->add_class("pad-button");
            btn->signal_clicked().connect([this, i]() { focusedPadId = i; primary_activate_button(i); });

            auto focusController = Gtk::EventControllerFocus::create();
            focusController->signal_enter().connect([this, i]() { focusedPadId = i; });
            btn->add_controller(focusController);

            auto rightClick = Gtk::GestureClick::create();
            rightClick->set_button(3);
            rightClick->signal_pressed().connect([this, i](int, double, double) {
                focusedPadId = i;
                // Defer the editor until after GTK finishes handling the click.
                Glib::signal_idle().connect_once([this, i]() { edit_button(i); });
            });
            btn->add_controller(rightClick);

            add_audio_drop_target(*btn, i);

            grid.attach(*btn, i % state.gridColumns, i / state.gridColumns, 1, 1);
            padButtons.push_back(btn);
        }
    }

    void open_settings_dialog() {
        Gtk::Dialog dialog("Settings", *this, true);
        dialog.add_button("Cancel", Gtk::ResponseType::CANCEL);
        dialog.add_button("Save", Gtk::ResponseType::OK);
        dialog.set_default_response(Gtk::ResponseType::OK);
        dialog.set_resizable(true);
        dialog.set_default_size(760, 520);
        auto* box = dialog.get_content_area();
        box->set_spacing(12);
        ::set_margin(*box, 12);

        auto make_section = [](const Glib::ustring& title, Gtk::Widget& child) {
            auto* frame = Gtk::make_managed<Gtk::Frame>(title);
            frame->set_child(child);
            return frame;
        };

        auto* generalBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*generalBox, 10);
        Gtk::Button themeButton("Theme: " + to_string(state.themeMode));

        auto* gridSizeRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        Gtk::Label gridSizeLabel("Grid Size:");
        gridSizeLabel.set_xalign(0.0f);
        Gtk::Label rowsLabel("Rows");
        Gtk::SpinButton rowsSpin;
        rowsSpin.set_range(1, 8);
        rowsSpin.set_increments(1, 1);
        rowsSpin.set_digits(0);
        rowsSpin.set_value(state.gridRows);
        rowsSpin.set_size_request(70, -1);
        Gtk::Label columnsLabel("Columns");
        Gtk::SpinButton columnsSpin;
        columnsSpin.set_range(1, 8);
        columnsSpin.set_increments(1, 1);
        columnsSpin.set_digits(0);
        columnsSpin.set_value(state.gridColumns);
        columnsSpin.set_size_request(70, -1);
        gridSizeRow->append(gridSizeLabel );
        gridSizeRow->append(rowsLabel );
        gridSizeRow->append(rowsSpin );
        gridSizeRow->append(columnsLabel );
        gridSizeRow->append(columnsSpin );

        Gtk::Label note("Changing rows/columns resizes the pad grid. Pads outside a smaller grid are removed.");
        note.set_wrap(true);
        note.set_xalign(0.0f);
        generalBox->append(themeButton );
        generalBox->append(*gridSizeRow );
        generalBox->append(note );

        auto* midiBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*midiBox, 10);
        Gtk::CheckButton midiEnable("Enable MIDI input");
        midiEnable.set_active(state.midiEnabled);
        auto* midiRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        Gtk::Label midiPortLabel("Input Port");
        midiPortLabel.set_xalign(0.0f);
        Gtk::ComboBoxText midiPortCombo;
        auto midiPorts = midi_port_names();
        for (const auto& port : midiPorts) midiPortCombo.append(port, port);
        if (!state.midiPortName.empty()) midiPortCombo.set_active_id(state.midiPortName);
        if (midiPortCombo.get_active_id().empty() && !midiPorts.empty()) midiPortCombo.set_active(0);
        midiPortCombo.set_sensitive(!midiPorts.empty());
        midiPortCombo.set_hexpand(true);
        Gtk::Label midiHelp;
#ifdef HAVE_RTMIDI
        midiHelp.set_text(midiPorts.empty() ? "No MIDI input ports found. Start VMPK or connect a device, then reopen Settings." : "Use VMPK or a hardware controller, then assign notes with Learn MIDI in a pad.");
#else
        midiHelp.set_text("MIDI support was not built. Install librtmidi-dev and rebuild.");
#endif
        midiHelp.set_xalign(0.0f);
        midiHelp.set_wrap(true);
        midiRow->append(midiPortLabel );
        midiRow->append(midiPortCombo );
        Gtk::Label midiLastLabel;
        midiLastLabel.set_xalign(0.0f);
        midiLastLabel.set_text("Last MIDI: " + state.lastMidiEvent);
        auto* midiMonitorRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        Gtk::Button midiClearMonitor("Clear Monitor");
        midiMonitorRow->append(midiLastLabel );
        midiMonitorRow->append(midiClearMonitor );

        midiBox->append(midiEnable );
        midiBox->append(*midiRow );
        midiBox->append(midiHelp );
        midiBox->append(*midiMonitorRow );

        auto* groupsBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*groupsBox, 10);
        Gtk::Label groupHelp("Define up to 6 named groups. Exclusive groups can stop, fade, or crossfade. Enable ducking on a group to attenuate its targets while that group is active; leave Targets blank to duck all other groups.");
        groupHelp.set_xalign(0.0f);
        groupHelp.set_wrap(true);
        groupsBox->append(groupHelp );

        constexpr int GROUP_COL_NAME = 180;
        constexpr int GROUP_COL_TYPE = 115;
        constexpr int GROUP_COL_COLOR = 90;
        constexpr int GROUP_COL_TRANSITION = 110;
        constexpr int GROUP_COL_FADE_OUT = 150;
        constexpr int GROUP_COL_FADE_IN = 150;
        constexpr int GROUP_COL_DUCK = 55;
        constexpr int GROUP_COL_TARGETS = 200;
        constexpr int GROUP_COL_AMOUNT = 65;

        auto configure_group_header = [](Gtk::Label& label, int width) {
            label.set_size_request(width, -1);
            label.set_xalign(0.0f);
            label.set_hexpand(false);
        };
        auto configure_group_widget = [](Gtk::Widget& widget, int width) {
            widget.set_size_request(width, -1);
            widget.set_hexpand(false);
        };

        auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        header->set_homogeneous(false);
        Gtk::Label hName("Name");
        Gtk::Label hType("Type");
        Gtk::Label hColor("Color");
        Gtk::Label hTransition("Transition");
        Gtk::Label hFadeOut("Fade Out [ms]");
        Gtk::Label hFadeIn("Fade In [ms]");
        Gtk::Label hDuck("Ducks");
        Gtk::Label hTargets("Duck Targets");
        Gtk::Label hAmount("Duck Amt [dB]");
        configure_group_header(hName, GROUP_COL_NAME);
        configure_group_header(hType, GROUP_COL_TYPE);
        configure_group_header(hColor, GROUP_COL_COLOR);
        configure_group_header(hTransition, GROUP_COL_TRANSITION);
        configure_group_header(hFadeOut, GROUP_COL_FADE_OUT);
        configure_group_header(hFadeIn, GROUP_COL_FADE_IN);
        configure_group_header(hDuck, GROUP_COL_DUCK);
        configure_group_header(hTargets, GROUP_COL_TARGETS);
        configure_group_header(hAmount, GROUP_COL_AMOUNT);
        header->append(hName );
        header->append(hType );
        header->append(hColor );
        header->append(hTransition );
        header->append(hFadeOut );
        header->append(hFadeIn );
        header->append(hDuck );
        header->append(hTargets );
        header->append(hAmount );
        groupsBox->append(*header );

        struct GroupRowWidgets {
            Gtk::Entry* name = nullptr;
            Gtk::ComboBoxText* type = nullptr;
            Gtk::ComboBoxText* color = nullptr;
            Gtk::ComboBoxText* transition = nullptr;
            Gtk::SpinButton* fadeOut = nullptr;
            Gtk::SpinButton* fadeIn = nullptr;
            Gtk::CheckButton* duckEnabled = nullptr;
            Gtk::Entry* duckTargets = nullptr;
            Gtk::SpinButton* duckAmount = nullptr;
        };
        auto join_targets = [](const std::set<std::string>& targets) {
            std::ostringstream out;
            bool first = true;
            for (const auto& t : targets) {
                if (!first) out << ", ";
                out << t;
                first = false;
            }
            return out.str();
        };

        std::vector<GroupRowWidgets> groupRows;
        const int groupRowCount = MAX_PAD_GROUPS;
        for (int i = 0; i < groupRowCount; ++i) {
            auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            row->set_homogeneous(false);
            auto* name = Gtk::make_managed<Gtk::Entry>();
            configure_group_widget(*name, GROUP_COL_NAME);
            name->set_placeholder_text("New group name");
            auto* type = Gtk::make_managed<Gtk::ComboBoxText>();
            type->append("play", display_group_type(PadGroupType::Play));
            type->append("exclusive", display_group_type(PadGroupType::Exclusive));
            configure_group_widget(*type, GROUP_COL_TYPE);
            auto* color = Gtk::make_managed<Gtk::ComboBoxText>();
            for (const auto& colorId : pad_color_ids()) color->append(colorId, display_pad_color(colorId));
            configure_group_widget(*color, GROUP_COL_COLOR);
            auto* transition = Gtk::make_managed<Gtk::ComboBoxText>();
            transition->append("stop", display_group_transition(GroupTransition::Stop));
            transition->append("fade", display_group_transition(GroupTransition::Fade));
            transition->append("crossfade", display_group_transition(GroupTransition::Crossfade));
            configure_group_widget(*transition, GROUP_COL_TRANSITION);
            auto* fadeOut = Gtk::make_managed<Gtk::SpinButton>();
            fadeOut->set_range(0, 10000);
            fadeOut->set_increments(50, 500);
            configure_group_widget(*fadeOut, GROUP_COL_FADE_OUT);
            fadeOut->set_numeric(true);
            auto* fadeIn = Gtk::make_managed<Gtk::SpinButton>();
            fadeIn->set_range(0, 10000);
            fadeIn->set_increments(50, 500);
            configure_group_widget(*fadeIn, GROUP_COL_FADE_IN);
            fadeIn->set_numeric(true);
            auto* duckEnabled = Gtk::make_managed<Gtk::CheckButton>();
            configure_group_widget(*duckEnabled, GROUP_COL_DUCK);
            auto* duckTargets = Gtk::make_managed<Gtk::Entry>();
            duckTargets->set_placeholder_text("blank = all others");
            configure_group_widget(*duckTargets, GROUP_COL_TARGETS);
            auto* duckAmount = Gtk::make_managed<Gtk::SpinButton>();
            duckAmount->set_range(0, 36);
            duckAmount->set_increments(1, 3);
            configure_group_widget(*duckAmount, GROUP_COL_AMOUNT);
            duckAmount->set_numeric(true);

            if (i < static_cast<int>(state.groups.size())) {
                const auto& g = state.groups[checked_index(i)];
                name->set_text(g.name);
                type->set_active_id(to_string(g.type));
                color->set_active_id(normalize_pad_color(g.color));
                transition->set_active_id(to_string(g.transition));
                fadeOut->set_value(clamp_fade_ms(g.fadeOutMs));
                fadeIn->set_value(clamp_fade_ms(g.fadeInMs));
                duckEnabled->set_active(g.duckEnabled);
                duckTargets->set_text(join_targets(g.duckTargets));
                duckAmount->set_value(clamp_duck_amount_db(g.duckAmountDb));
            } else {
                type->set_active_id("play");
                color->set_active_id("default");
                transition->set_active_id("stop");
                fadeOut->set_value(500);
                fadeIn->set_value(500);
                duckEnabled->set_active(false);
                duckAmount->set_value(12);
            }
            row->append(*name );
            row->append(*type );
            row->append(*color );
            row->append(*transition );
            row->append(*fadeOut );
            row->append(*fadeIn );
            row->append(*duckEnabled );
            row->append(*duckTargets );
            row->append(*duckAmount );
            groupsBox->append(*row );
            groupRows.push_back({name, type, color, transition, fadeOut, fadeIn, duckEnabled, duckTargets, duckAmount});
        }

        auto* notebook = Gtk::make_managed<Gtk::Notebook>();
        notebook->set_scrollable(true);

        auto* generalTab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
        ::set_margin(*generalTab, 10);
        generalTab->append(*make_section("General", *generalBox) );
        generalTab->append(*make_section("MIDI", *midiBox) );

        auto* groupsTab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*groupsTab, 10);
        groupsTab->append(*make_section("Groups", *groupsBox) );

        notebook->append_page(*generalTab, "General");
        notebook->append_page(*groupsTab, "Groups");
        box->append(*notebook );

        themeButton.signal_clicked().connect([&]() {
            state.themeMode = (state.themeMode == AppThemeMode::Light) ? AppThemeMode::Dark : AppThemeMode::Light;
            themeButton.set_label("Theme: " + to_string(state.themeMode));
            state.status = "Theme: " + to_string(state.themeMode);
            repository.save(state);
            apply_theme();
            refresh_ui();
        });

        midiClearMonitor.signal_clicked().connect([&]() {
            state.lastMidiEvent = "None";
            state.lastMidiChannel = -1;
            state.lastMidiNote = -1;
            state.lastMidiVelocity = 0;
            midiLastLabel.set_text("Last MIDI: " + state.lastMidiEvent);
        });
        sigc::connection midiMonitorTimer = Glib::signal_timeout().connect([&]() {
            midiLastLabel.set_text("Last MIDI: " + state.lastMidiEvent);
            return true;
        }, 120);

        int response = run_dialog_blocking(dialog);
        midiMonitorTimer.disconnect();
        if (response != Gtk::ResponseType::OK && response != Gtk::ResponseType::APPLY && response != Gtk::ResponseType::ACCEPT) return;

        auto split_targets = [](const std::string& raw) {
            std::set<std::string> targets;
            std::stringstream ss(raw);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::string t = normalize_group_name(item);
                if (!t.empty()) targets.insert(t);
            }
            return targets;
        };

        std::vector<PadGroup> newGroups;
        for (const auto& row : groupRows) {
            PadGroup g;
            g.name = normalize_group_name(row.name->get_text());
            if (g.name.empty() || find_group(newGroups, g.name)) continue;
            g.type = group_type_from_string(row.type->get_active_id());
            g.color = normalize_pad_color(row.color->get_active_id());
            g.transition = group_transition_from_string(row.transition->get_active_id());
            g.fadeOutMs = clamp_fade_ms(row.fadeOut->get_value_as_int());
            g.fadeInMs = clamp_fade_ms(row.fadeIn->get_value_as_int());
            g.duckEnabled = row.duckEnabled->get_active();
            g.duckTargets = split_targets(row.duckTargets->get_text());
            g.duckTargets.erase(g.name);
            g.duckAmountDb = clamp_duck_amount_db(static_cast<float>(row.duckAmount->get_value()));
            g.duckAttackMs = 100;
            g.duckReleaseMs = 750;
            if (static_cast<int>(newGroups.size()) >= MAX_PAD_GROUPS) break;
            newGroups.push_back(g);
        }

        bool groupsChanged = newGroups.size() != state.groups.size();
        if (!groupsChanged) {
            for (size_t i = 0; i < newGroups.size(); ++i) {
                if (newGroups[i].name != state.groups[i].name ||
                    newGroups[i].type != state.groups[i].type ||
                    normalize_pad_color(newGroups[i].color) != normalize_pad_color(state.groups[i].color) ||
                    newGroups[i].transition != state.groups[i].transition ||
                    clamp_fade_ms(newGroups[i].fadeOutMs) != clamp_fade_ms(state.groups[i].fadeOutMs) ||
                    clamp_fade_ms(newGroups[i].fadeInMs) != clamp_fade_ms(state.groups[i].fadeInMs) ||
                    newGroups[i].duckEnabled != state.groups[i].duckEnabled ||
                    newGroups[i].duckTargets != state.groups[i].duckTargets ||
                    std::abs(clamp_duck_amount_db(newGroups[i].duckAmountDb) - clamp_duck_amount_db(state.groups[i].duckAmountDb)) > 0.01f) {
                    groupsChanged = true;
                    break;
                }
            }
        }
        if (groupsChanged) {
            state.groups = newGroups;
            for (auto& b : state.buttons) {
                if (!b.exclusiveGroup.empty() && !find_group(state.groups, b.exclusiveGroup)) b.exclusiveGroup.clear();
            }
            state.status = "Groups updated";
        }

        int newRows = clamp_grid_size(rowsSpin.get_value_as_int());
        int newColumns = clamp_grid_size(columnsSpin.get_value_as_int());
        bool gridChanged = newRows != state.gridRows || newColumns != state.gridColumns;
        if (gridChanged) {
            int oldCount = static_cast<int>(state.buttons.size());
            int newCount = newRows * newColumns;
            audio.stop_all();
            if (newCount < oldCount) {
                for (int i = newCount; i < oldCount; ++i) { cancel_reverse_job(i); state.buttons[checked_index(i)] = repository.clear_audio(state.buttons[checked_index(i)]); }
            }
            state.gridRows = newRows;
            state.gridColumns = newColumns;
            ensure_button_count(state.buttons, state.gridRows * state.gridColumns);
            state.status = "Grid: " + std::to_string(state.gridRows) + " x " + std::to_string(state.gridColumns);
        }

        const std::string activeMidiPortId = midiPortCombo.get_active_id();
        bool midiChanged = state.midiEnabled != midiEnable.get_active() || state.midiPortName != activeMidiPortId;
        if (midiChanged) {
            state.midiEnabled = midiEnable.get_active();
            state.midiPortName = activeMidiPortId;
            open_configured_midi_port();
            if (state.midiEnabled && !state.midiPortName.empty()) state.status = "MIDI input: " + state.midiPortName;
            else if (!state.midiEnabled) state.status = "MIDI input disabled";
        }

        if (groupsChanged || gridChanged || midiChanged) {
            repository.save(state);
            if (gridChanged) rebuild_pad_grid();
            refresh_ui();
        }
    }

    void refresh_playing_state_until_stopped(int id) {
        refresh_pad_grid_ui();
        refresh_mixer_ui();
        Glib::signal_timeout().connect([this, id]() {
            const bool playing = audio.is_key_playing(id);
            refresh_pad_grid_ui();
            update_mixer_progress_ui();
            return playing;
        }, 500);
    }

    bool is_exclusive_group(const std::string& name) const {
        if (name.empty()) return false;
        if (const auto* g = find_group(state.groups, name)) return g->type == PadGroupType::Exclusive;
        return true; // Legacy safety: unknown non-empty groups behave like the old exclusive groups.
    }

    void primary_activate_button(int id) {
        if (id < 0 || id >= static_cast<int>(state.buttons.size())) return;
        auto& b = state.buttons.at(checked_index(id));
        if (!b.assigned) { set_status("No audio assigned. Open a pad and choose Load Audio."); return; }
        const PadGroup* activeGroup = find_group(state.groups, b.exclusiveGroup);
        GroupTransition groupTransition = activeGroup ? activeGroup->transition : GroupTransition::Stop;
        int groupFadeOutMs = activeGroup ? clamp_fade_ms(activeGroup->fadeOutMs) : b.fadeOutMs;
        int groupFadeInMs = activeGroup ? clamp_fade_ms(activeGroup->fadeInMs) : b.fadeInMs;
        if (is_exclusive_group(b.exclusiveGroup)) {
            for (const auto& other : state.buttons) {
                if (other.id != b.id && other.exclusiveGroup == b.exclusiveGroup) {
                    if (groupTransition == GroupTransition::Stop) audio.stop_key_immediate(other.id);
                    else audio.stop_key_with_fade_ms(other.id, groupFadeOutMs);
                }
            }
        }
        if (b.playbackDirection == PlaybackDirection::Reverse && !ensure_reverse_ready_or_start(id)) return;
        try {
            auto file = repository.playback_file(b);
            double playTrimStart = b.trimStart;
            double playTrimEnd = b.trimEnd;
            if (b.playbackDirection == PlaybackDirection::Reverse) {
                double duration = b.durationSeconds;
                if (duration > 0.0) {
                    double effectiveStart = std::min(clamp_time_seconds(b.trimStart), duration);
                    double effectiveEnd = b.trimEnd > 0.0 ? std::min(b.trimEnd, duration) : duration;
                    if (effectiveEnd <= effectiveStart) {
                        effectiveStart = 0.0;
                        effectiveEnd = duration;
                    }
                    playTrimStart = std::max(0.0, duration - effectiveEnd);
                    playTrimEnd = std::max(0.0, duration - effectiveStart);
                }
            }
            // AudioEngine treats trimEnd > 0 as a segment end and queues a seek after preroll.
            // Since trimEnd now defaults to the full duration, pass 0 when the whole tail is intended.
            // Otherwise an untrimmed pad starts immediately, then seeks back to the start ~150ms later,
            // which sounds like a duplicate/delay effect on a single click.
            if (b.durationSeconds > 0.0 && playTrimEnd > 0.0 && std::abs(playTrimEnd - b.durationSeconds) <= 0.01) {
                playTrimEnd = 0.0;
            }
            int effectiveFadeInMs = (is_exclusive_group(b.exclusiveGroup) && groupTransition == GroupTransition::Crossfade) ? groupFadeInMs : b.fadeInMs;
            audio.play(b.id, file, b.volume, static_cast<float>(normalization_gain_linear(b)), b.playbackSpeed, b.playbackPitch, b.playbackMode, b.pan, b.exclusiveGroup, playTrimStart, playTrimEnd, effectiveFadeInMs, b.fadeOutMs,
                       [this](const std::string& msg) { Glib::signal_idle().connect_once([this, msg]() { set_status(msg); }); });
            set_status(b.playbackDirection == PlaybackDirection::Reverse ? "Reverse audio playback started" : "Audio playback started");
            refresh_playing_state_until_stopped(b.id);
        } catch (const std::exception& ex) {
            set_status(ex.what());
        }
    }

    struct ReversePrepareResult {
        bool ok = false;
        std::string message;
        std::string outputPath;
    };

    void start_reverse_prepare_job(int id, const SoundButton& b, const std::string& preparingMessage = um::reverse_generating(), bool showReadyStatus = true) {
        if (!b.assigned || b.storedFilename.empty()) return;
        if (!ffmpegAvailable) {
            set_status(um::reverse_unavailable_missing_ffmpeg());
            return;
        }
        if (repository.reverse_audio_is_ready(b)) {
            reverseErrors.erase(id);
            return;
        }
        reverseErrors.erase(id);

        {
            std::lock_guard<std::mutex> lock(reverseJobsMutex);
            auto it = reverseJobs.find(id);
            if (it != reverseJobs.end()) {
                if (it->second.storedFilename == b.storedFilename) {
                    if (!preparingMessage.empty()) set_status(preparingMessage);
                    refresh_ui();
                    return;
                }
                it->second.handle.cancel();
                reverseJobs.erase(it);
                set_status("Cancelling previous reverse audio preparation");
            }
        }

        if (!preparingMessage.empty()) set_status(preparingMessage);
        refresh_ui();
        auto alive = windowAlive;
        auto handle = taskRunner.submit(
            "reverse-audio",
            [rootDir = repository.root_dir(), b](pulsepad::CancellationToken token) {
                BoardRepository workerRepository(rootDir);
                auto cancelFlag = token.cancel_flag();
                auto output = workerRepository.ensure_reverse_audio_for_button(b, cancelFlag.get());
                return ReversePrepareResult{true, um::reverse_ready(), output.string()};
            },
            [this, alive, id, storedFilename = b.storedFilename, showReadyStatus](TaskOutcome<ReversePrepareResult> outcome) mutable {
                if (!alive->load()) return;
                erase_reverse_job_if_current(id, storedFilename);
                if (outcome.status == TaskStatus::Cancelled) {
                    reverseErrors[id] = "cancelled";
                    set_status("Reverse audio cancelled.");
                    refresh_ui();
                    return;
                }
                const bool ok = outcome.succeeded() && outcome.value.ok;
                if (ok) {
                    reverseErrors.erase(id);
                    const auto& current = state.buttons.at(checked_index(id));
                    if (!repository.reverse_audio_is_ready(current)) {
                        reverseErrors[id] = "generated file was not accepted as ready";
                        set_status("Reverse audio finished but cache is still not ready; check stored file timestamps");
                    } else if (showReadyStatus) {
                        set_status(outcome.value.message);
                    }
                    refresh_ui();
                    return;
                }
                reverseErrors[id] = outcome.userMessage.empty() ? "unknown failure" : outcome.userMessage;
                set_status(um::reverse_failed(reverseErrors[id]));
                refresh_ui();
            });

        {
            std::lock_guard<std::mutex> lock(reverseJobsMutex);
            reverseJobs[id] = ReverseJob{id, b.storedFilename, handle};
        }
        refresh_ui();
    }

    bool ensure_reverse_ready_or_start(int id) {
        SoundButton b = state.buttons.at(checked_index(id));
        if (repository.reverse_audio_is_ready(b)) return true;
        start_reverse_prepare_job(id, b);
        return false;
    }

    void edit_button(int id) {
        SoundButton originalButton = state.buttons.at(checked_index(id));
        SoundButton b = originalButton;
        Gtk::Dialog dialog("Edit " + b.label, *this, true);
        dialog.add_button("Cancel", Gtk::ResponseType::CANCEL);
        dialog.add_button("Save", Gtk::ResponseType::OK);
        dialog.set_default_response(Gtk::ResponseType::OK);
        dialog.set_resizable(true);
        auto* box = dialog.get_content_area();
        box->set_spacing(12);
        ::set_margin(*box, 12);

        auto make_section = [](const Glib::ustring& title, Gtk::Widget& child) {
            auto* frame = Gtk::make_managed<Gtk::Frame>(title);
            frame->set_child(child);
            return frame;
        };

        auto* padBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*padBox, 10);
        Gtk::Label labelCaption("Label");
        labelCaption.set_xalign(0.0f);
        Gtk::Entry labelEntry;
        labelEntry.set_text(b.label);
        Gtk::Label colorCaption("Color (when no group is selected)");
        colorCaption.set_xalign(0.0f);
        Gtk::ComboBoxText colorCombo;
        for (const auto& colorId : pad_color_ids()) colorCombo.append(colorId, display_pad_color(colorId));
        colorCombo.set_active_id(normalize_pad_color(b.color));
        Gtk::Button loadButton(b.originalFilename.empty() ? "Choose Audio File" : "Audio File: " + b.originalFilename);
        Gtk::Button clearButton("Remove Audio");
        Gtk::Button resetLabelButton("Reset Label");
        labelEntry.set_tooltip_text("Name shown on the pad");
        colorCombo.set_tooltip_text("Pad color when the pad is not using a group color");
        loadButton.set_tooltip_text("Assign an audio file to this pad");
        clearButton.set_tooltip_text("Remove this pad audio and related generated files");
        resetLabelButton.set_tooltip_text("Use the audio filename as the pad label");
        auto* padButtonRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        padButtonRow->append(loadButton );
        padButtonRow->append(clearButton );
        padButtonRow->append(resetLabelButton );
        padBox->append(labelCaption );
        padBox->append(labelEntry );
        padBox->append(colorCaption );
        padBox->append(colorCombo );
        padBox->append(*padButtonRow );

        auto* triggerBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*triggerBox, 10);
        Gtk::Label groupCaption("Group");
        groupCaption.set_xalign(0.0f);
        Gtk::ComboBoxText groupCombo;
        groupCombo.append("", "None");
        for (const auto& g : state.groups) {
            groupCombo.append(g.name, g.name + " (" + display_group_type(g.type) + ")");
        }
        if (!b.exclusiveGroup.empty() && !find_group(state.groups, b.exclusiveGroup)) {
            groupCombo.append(b.exclusiveGroup, b.exclusiveGroup + " (Legacy exclusive)");
        }
        groupCombo.set_active_id(b.exclusiveGroup);
        Gtk::Label groupHelp("Groups are defined in Settings. Exclusive groups stop each other; play groups only share color/organization.");
        groupHelp.set_xalign(0.0f);
        groupHelp.set_wrap(true);
        Gtk::Label hotkeyLabel;
        hotkeyLabel.set_xalign(0.0f);
        Gtk::Button learnHotkeyButton("Learn Hotkey");
        Gtk::Button clearHotkeyButton("Clear Hotkey");
        groupCombo.set_tooltip_text("Choose how this pad interacts with grouped pads");
        learnHotkeyButton.set_tooltip_text("Press a keyboard key to trigger this pad");
        clearHotkeyButton.set_tooltip_text("Remove the keyboard shortcut for this pad");
        auto* hotkeyRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        hotkeyRow->append(learnHotkeyButton );
        hotkeyRow->append(clearHotkeyButton );
        Gtk::Label midiLabel;
        midiLabel.set_xalign(0.0f);
        Gtk::Button learnMidiButton("Learn MIDI");
        Gtk::Button clearMidiButton("Clear MIDI");
        learnMidiButton.set_tooltip_text("Listen for the next MIDI note and map it to this pad");
        clearMidiButton.set_tooltip_text("Remove the MIDI note mapping for this pad");
        auto* midiTriggerRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        midiTriggerRow->append(learnMidiButton );
        midiTriggerRow->append(clearMidiButton );
        triggerBox->append(groupCaption );
        triggerBox->append(groupCombo );
        triggerBox->append(groupHelp );
        triggerBox->append(hotkeyLabel );
        triggerBox->append(*hotkeyRow );
        triggerBox->append(midiLabel );
        triggerBox->append(*midiTriggerRow );
        bool learningHotkey = false;
        guint learnedHotkeyKeyval = b.hotkeyKeyval;
        std::string learnedHotkeyLabel = b.hotkeyLabel;
        int learnedMidiChannel = b.midiChannel;
        int learnedMidiNote = b.midiNote;

        auto* playbackBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*playbackBox, 10);
        Gtk::Label modeCaption("Mode");
        modeCaption.set_xalign(0.0f);
        Gtk::ComboBoxText modeCombo;
        modeCombo.append("playthrough", display_label(PlaybackMode::PlayThrough));
        modeCombo.append("loop", display_label(PlaybackMode::Loop));
        modeCombo.append("retrigger", display_label(PlaybackMode::Retrigger));
        if (b.playbackMode == PlaybackMode::Loop) modeCombo.set_active_id("loop");
        else if (b.playbackMode == PlaybackMode::Retrigger) modeCombo.set_active_id("retrigger");
        else modeCombo.set_active_id("playthrough");
        Gtk::Label directionCaption("Direction");
        directionCaption.set_xalign(0.0f);
        Gtk::ComboBoxText directionCombo;
        directionCombo.append("forward", display_label(PlaybackDirection::Forward));
        directionCombo.append("reverse", display_label(PlaybackDirection::Reverse));
        directionCombo.set_active_id(b.playbackDirection == PlaybackDirection::Reverse ? "reverse" : "forward");
        directionCombo.set_tooltip_text("Reverse generates a reversed copy before playback");
        modeCombo.set_tooltip_text("Choose whether this pad plays once, loops, or restarts when triggered again");

        Gtk::Label volLabel, speedLabel, panLabel, fadeInLabel, fadeOutLabel;
        Gtk::Scale vol(Gtk::Orientation::HORIZONTAL), speed(Gtk::Orientation::HORIZONTAL), pan(Gtk::Orientation::HORIZONTAL), fadeIn(Gtk::Orientation::HORIZONTAL), fadeOut(Gtk::Orientation::HORIZONTAL);
        vol.set_range(PAD_VOLUME_MIN_DB, PAD_VOLUME_MAX_DB);
        speed.set_range(0, 2);
        pan.set_range(-1, 1);
        vol.add_mark(PAD_VOLUME_MIN_DB, Gtk::PositionType::BOTTOM, "Mute");
        vol.add_mark(-24.0, Gtk::PositionType::BOTTOM, "-24");
        vol.add_mark(-12.0, Gtk::PositionType::BOTTOM, "-12");
        vol.add_mark(-6.0, Gtk::PositionType::BOTTOM, "-6");
		vol.add_mark(-3.0, Gtk::PositionType::BOTTOM, "-3");
        vol.add_mark(0.0, Gtk::PositionType::BOTTOM, "0 dB");
		vol.add_mark(3.0, Gtk::PositionType::BOTTOM, "+3");
        vol.add_mark(6.0, Gtk::PositionType::BOTTOM, "+6");
        vol.add_mark(PAD_VOLUME_MAX_DB, Gtk::PositionType::BOTTOM, "+12");
        speed.add_mark(0.0, Gtk::PositionType::BOTTOM, "0.0");
        speed.add_mark(0.5, Gtk::PositionType::BOTTOM, "0.5");
        speed.add_mark(1.0, Gtk::PositionType::BOTTOM, "1.0x");
        speed.add_mark(1.5, Gtk::PositionType::BOTTOM, "1.5");
        speed.add_mark(2.0, Gtk::PositionType::BOTTOM, "2.0");
        pan.add_mark(-1.0, Gtk::PositionType::BOTTOM, "Left");
        pan.add_mark(0.0, Gtk::PositionType::BOTTOM, "Center");
        pan.add_mark(1.0, Gtk::PositionType::BOTTOM, "Right");
        fadeIn.set_range(0, 10000);
        fadeOut.set_range(0, 10000);
        vol.set_increments(0.1, 1.0);
        speed.set_increments(0.01, 0.1);
        pan.set_increments(0.01, 0.1);
        fadeIn.set_increments(50, 500);
        fadeOut.set_increments(50, 500);
        vol.property_draw_value() = false;
        speed.property_draw_value() = false;
        pan.property_draw_value() = false;
        fadeIn.property_draw_value() = false;
        fadeOut.property_draw_value() = false;
        vol.set_value(linear_volume_to_db(b.volume));
        speed.set_value(b.playbackSpeed);
        pan.set_value(b.pan);
        fadeIn.set_value(b.fadeInMs);
        fadeOut.set_value(b.fadeOutMs);

        playbackBox->append(modeCaption );
        playbackBox->append(modeCombo );
        playbackBox->append(directionCaption );
        playbackBox->append(directionCombo );
        playbackBox->append(volLabel );
        playbackBox->append(vol );
        playbackBox->append(speedLabel );
        playbackBox->append(speed );
        playbackBox->append(panLabel );
        playbackBox->append(pan );
        auto* loudnessBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*loudnessBox, 10);
        Gtk::Label normalizationCaption("Mode");
        normalizationCaption.set_xalign(0.0f);
        Gtk::ComboBoxText normalizationCombo;
        normalizationCombo.append("off", display_label(NormalizationMode::Off));
        normalizationCombo.append("trimmed", display_label(NormalizationMode::TrimmedRegion));
        if (b.normalizationMode == NormalizationMode::TrimmedRegion) normalizationCombo.set_active_id("trimmed");
        else normalizationCombo.set_active_id("off");
        normalizationCombo.set_tooltip_text("Apply cached automatic loudness gain before the manual pad volume");
        Gtk::Label normalizationInfo;
        normalizationInfo.set_xalign(0.0f);
        normalizationInfo.set_wrap(true);

        playbackBox->append(fadeInLabel );
        playbackBox->append(fadeIn );
        playbackBox->append(fadeOutLabel );
        playbackBox->append(fadeOut );

        Gtk::Label loudnessHelp("Automatic gain is applied after the trim region and before the manual pad volume. Analysis is cached in the project and does not modify audio files.");
        loudnessHelp.set_xalign(0.0f);
        loudnessHelp.set_wrap(true);
        loudnessBox->append(loudnessHelp );
        loudnessBox->append(normalizationCaption );
        loudnessBox->append(normalizationCombo );
        loudnessBox->append(normalizationInfo );

        auto* trimBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        ::set_margin(*trimBox, 10);
        Gtk::Label trimStartLabel, trimEndLabel, durationLabel;
        trimStartLabel.set_xalign(0.0f);
        trimEndLabel.set_xalign(0.0f);
        durationLabel.set_xalign(0.0f);
        Gtk::SpinButton trimStart, trimEnd;
        double audioDuration = b.durationSeconds;
        double trimMax = audioDuration > 0.0 ? audioDuration : 300.0;
        trimStart.set_range(0, trimMax);
        trimEnd.set_range(0, trimMax);
        trimStart.set_increments(0.01, 1.0);
        trimEnd.set_increments(0.01, 1.0);
        trimStart.set_digits(2);
        trimEnd.set_digits(2);
        trimStart.set_width_chars(7);
        trimEnd.set_width_chars(7);
        if (audioDuration > 0.0) {
            b.trimStart = std::min(clamp_time_seconds(b.trimStart), audioDuration);
            b.trimEnd = clamp_time_seconds(b.trimEnd);
            if (b.trimEnd <= 0.0 || b.trimEnd > audioDuration) b.trimEnd = audioDuration;
            if (b.trimEnd <= b.trimStart) { b.trimStart = 0.0; b.trimEnd = audioDuration; }
        }
        trimStart.set_value(b.trimStart);
        trimEnd.set_value(audioDuration > 0.0 && b.trimEnd <= 0.0 ? audioDuration : b.trimEnd);
        Gtk::Button resetTrimButton("Reset");
        Gtk::Button playPreviewButton("Play Preview");
        Gtk::Button stopPreviewButton("Stop");
        Gtk::Button setStartButton("Set Start");
        Gtk::Button setEndButton("Set End");
        trimStart.set_tooltip_text("Trim start time in seconds");
        trimEnd.set_tooltip_text("Trim end time in seconds");
        resetTrimButton.set_tooltip_text("Reset trim to the full audio length");
        setStartButton.set_tooltip_text("Set trim start to the current playhead");
        setEndButton.set_tooltip_text("Set trim end to the current playhead");
        playPreviewButton.set_tooltip_text("Preview only the selected trim range");
        stopPreviewButton.set_tooltip_text("Stop trim preview playback");
        double playheadSeconds = b.trimStart;
        bool previewActive = false;
        const int previewKey = PREVIEW_KEY_BASE - id;
        Gtk::DrawingArea waveformArea;
        waveformArea.set_size_request(-1, 260);
        Gtk::Label waveformHelp("Waveform: click/drag nearest trim handle");
        waveformHelp.set_xalign(0.0f);
        auto dialogAlive = std::make_shared<std::atomic<bool>>(true);
        auto waveformGeneration = std::make_shared<std::atomic<unsigned int>>(0);
        auto waveformTask = std::make_shared<TaskHandle>();
        bool waveformBusy = false;
        std::vector<double> waveformPeaks;
        int activeTrimHandle = 0; // 0=cursor, 1=start, 2=end
        auto trim_editor_reversed = [&]() {
            return directionCombo.get_active_id() == "reverse";
        };
        auto clamped_trim_start_value = [&]() {
            if (audioDuration <= 0.0) return 0.0;
            return std::max(0.0, std::min(audioDuration, clamp_time_seconds(trimStart.get_value())));
        };
        auto clamped_trim_end_value = [&]() {
            if (audioDuration <= 0.0) return 0.0;
            double end = clamp_time_seconds(trimEnd.get_value());
            if (end <= 0.0 || end > audioDuration) end = audioDuration;
            return std::max(0.0, std::min(audioDuration, end));
        };
        auto display_trim_range = [&]() {
            double sourceStart = clamped_trim_start_value();
            double sourceEnd = clamped_trim_end_value();
            if (sourceEnd <= sourceStart) {
                sourceStart = 0.0;
                sourceEnd = audioDuration;
            }
            if (!trim_editor_reversed()) return std::pair<double, double>{sourceStart, sourceEnd};
            return std::pair<double, double>{std::max(0.0, audioDuration - sourceEnd), std::min(audioDuration, audioDuration - sourceStart)};
        };
        auto setWaveformBusy = [&]() {
            const bool canUseTrim = !waveformBusy && audioDuration > 0.0;
            resetTrimButton.set_sensitive(canUseTrim);
            setStartButton.set_sensitive(canUseTrim);
            setEndButton.set_sensitive(canUseTrim);
            playPreviewButton.set_sensitive(canUseTrim);
        };
        auto reloadWaveform = [&]() {
            waveformPeaks.clear();
            waveformArea.queue_draw();
            waveformGeneration->fetch_add(1);
            waveformBusy = false;
            setWaveformBusy();

            if (!b.assigned || b.storedFilename.empty()) {
                waveformHelp.set_text("No audio loaded. Use Load Audio on the Pad tab first.");
                return;
            }

            const auto file = repository.sound_file(b);
            if (!fs::exists(file)) {
                waveformHelp.set_text(um::audio_missing());
                return;
            }
            if (audioDuration <= 0.0) {
                waveformHelp.set_text(um::duration_unknown(ffprobeAvailable));
                return;
            }
            if (!ffmpegAvailable) {
                waveformHelp.set_text(um::waveform_unavailable_missing_ffmpeg());
                return;
            }

            const unsigned int generation = waveformGeneration->load();
            const double duration = audioDuration;
            waveformBusy = true;
            setWaveformBusy();
            waveformHelp.set_text("Generating waveform...");
            auto alive = windowAlive;
            if (*waveformTask) waveformTask->cancel();
            *waveformTask = taskRunner.submit(
                "waveform-generate",
                [file, duration](pulsepad::CancellationToken token) {
                    if (token.cancellation_requested()) return std::vector<double>{};
                    return generate_waveform_peaks(file, duration);
                },
                [alive, dialogAlive, waveformGeneration, generation, &waveformPeaks, &waveformArea, &waveformHelp, &waveformBusy, &setWaveformBusy, &trim_editor_reversed](TaskOutcome<std::vector<double>> outcome) mutable {
                    if (!alive->load() || !dialogAlive->load() || waveformGeneration->load() != generation) return;
                    waveformBusy = false;
                    setWaveformBusy();
                    if (outcome.status == TaskStatus::Cancelled) return;
                    if (!outcome.succeeded()) {
                        waveformHelp.set_text(um::waveform_failed(outcome.userMessage));
                        waveformArea.queue_draw();
                        return;
                    }
                    waveformPeaks = std::move(outcome.value);
                    if (waveformPeaks.empty()) waveformHelp.set_text(um::waveform_failed("no waveform samples were produced"));
                    else waveformHelp.set_text(trim_editor_reversed()
                                                   ? "Waveform ready: reversed playback order; drag handles to trim, click waveform to preview/playhead."
                                                   : "Waveform ready: drag handles to trim, click waveform to preview/playhead.");
                    waveformArea.queue_draw();
                });
        };
        auto time_from_x = [&](double x) {
            if (audioDuration <= 0.0) return 0.0;
            int width = std::max(1, waveformArea.get_allocated_width());
            return std::max(0.0, std::min(audioDuration, (x / static_cast<double>(width)) * audioDuration));
        };
        auto set_playhead = [&](double t) {
            if (audioDuration <= 0.0) return;
            playheadSeconds = std::max(0.0, std::min(audioDuration, t));
            waveformArea.queue_draw();
        };
        auto set_trim_from_x = [&](double x, int handle) {
            if (audioDuration <= 0.0) return;
            double t = time_from_x(x);
            double minGap = std::min(0.01, audioDuration);
            if (!trim_editor_reversed()) {
                if (handle == 1) {
                    t = std::min(t, std::max(0.0, trimEnd.get_value() - minGap));
                    trimStart.set_value(t);
                    set_playhead(t);
                } else if (handle == 2) {
                    t = std::max(t, std::min(audioDuration, trimStart.get_value() + minGap));
                    trimEnd.set_value(t);
                    set_playhead(t);
                } else {
                    set_playhead(t);
                }
                return;
            }

            auto [displayStart, displayEnd] = display_trim_range();
            if (handle == 1) {
                t = std::min(t, std::max(0.0, displayEnd - minGap));
                trimEnd.set_value(std::max(0.0, std::min(audioDuration, audioDuration - t)));
                set_playhead(t);
            } else if (handle == 2) {
                t = std::max(t, std::min(audioDuration, displayStart + minGap));
                trimStart.set_value(std::max(0.0, std::min(audioDuration, audioDuration - t)));
                set_playhead(t);
            } else {
                set_playhead(t);
            }
        };
        waveformArea.set_draw_func([&](const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
            const int w = std::max(1, width);
            const int h = std::max(1, height);
            cr->set_source_rgb(0.10, 0.10, 0.10);
            cr->rectangle(0, 0, w, h);
            cr->fill();
            cr->set_source_rgb(0.25, 0.25, 0.25);
            cr->move_to(0, h / 2.0);
            cr->line_to(w, h / 2.0);
            cr->stroke();

            if (!waveformPeaks.empty()) {
                cr->set_source_rgb(0.55, 0.70, 0.95);
                const double center = h / 2.0;
                for (int x = 0; x < w; ++x) {
                    std::size_t idx = (checked_index(x) * waveformPeaks.size()) / checked_index(w);
                    if (idx >= waveformPeaks.size()) idx = waveformPeaks.size() - 1;
                    if (trim_editor_reversed()) idx = waveformPeaks.size() - 1 - idx;
                    double amp = waveformPeaks[idx] * (h * 0.42);
                    cr->move_to(x + 0.5, center - amp);
                    cr->line_to(x + 0.5, center + amp);
                }
                cr->stroke();
            } else {
                cr->set_source_rgb(0.75, 0.75, 0.75);
                cr->select_font_face("Sans", Cairo::ToyFontFace::Slant::NORMAL, Cairo::ToyFontFace::Weight::NORMAL);
                cr->set_font_size(12);
                std::string msg = b.assigned ? "Waveform unavailable" : "Load audio on the Pad tab";
                cr->move_to(10, h / 2.0 + 4);
                cr->show_text(msg);
            }

            if (audioDuration > 0.0) {
                auto [displayStart, displayEnd] = display_trim_range();
                double sx = (displayStart / audioDuration) * w;
                double ex = (displayEnd / audioDuration) * w;
                sx = std::max(0.0, std::min(static_cast<double>(w), sx));
                ex = std::max(0.0, std::min(static_cast<double>(w), ex));
                cr->set_source_rgba(0.20, 0.75, 0.35, 0.18);
                cr->rectangle(sx, 0, std::max(1.0, ex - sx), h);
                cr->fill();
                cr->set_line_width(2.0);
                cr->set_source_rgb(0.20, 0.85, 0.35);
                cr->move_to(sx, 0); cr->line_to(sx, h); cr->stroke();
                cr->set_source_rgb(0.95, 0.35, 0.25);
                cr->move_to(ex, 0); cr->line_to(ex, h); cr->stroke();

                double px = (playheadSeconds / audioDuration) * w;
                px = std::max(0.0, std::min(static_cast<double>(w), px));
                cr->set_line_width(1.5);
                cr->set_source_rgb(1.0, 0.85, 0.20);
                cr->move_to(px, 0); cr->line_to(px, h); cr->stroke();
                cr->arc(px, 8.0, 4.0, 0, 6.283185307179586);
                cr->fill();
            }
        });
        auto waveformClick = Gtk::GestureClick::create();
        waveformClick->set_button(1);
        waveformClick->signal_pressed().connect([&](int, double x, double) {
            if (audioDuration <= 0.0) return;
            int width = std::max(1, waveformArea.get_allocated_width());
            auto [displayStart, displayEnd] = display_trim_range();
            double sx = (displayStart / audioDuration) * width;
            double ex = (displayEnd / audioDuration) * width;
            double ds = std::abs(x - sx);
            double de = std::abs(x - ex);
            if (std::min(ds, de) <= 12.0) activeTrimHandle = ds <= de ? 1 : 2;
            else activeTrimHandle = 0;
            set_trim_from_x(x, activeTrimHandle);
        });
        waveformClick->signal_released().connect([&](int, double, double) { activeTrimHandle = 0; });
        waveformArea.add_controller(waveformClick);
        auto waveformMotion = Gtk::EventControllerMotion::create();
        waveformMotion->signal_motion().connect([&](double x, double) {
            if (activeTrimHandle == 0) return;
            set_trim_from_x(x, activeTrimHandle);
        });
        waveformArea.add_controller(waveformMotion);

        auto waveformTimerAlive = std::make_shared<bool>(true);
        Glib::signal_timeout().connect([&, waveformTimerAlive, previewKey]() {
            if (!*waveformTimerAlive) return false;
            if (audioDuration <= 0.0 || activeTrimHandle != 0) return true;

            std::optional<double> previewPos = audio.position_for_key(previewKey);
            if (previewActive && !previewPos) {
                previewActive = false;
                playheadSeconds = trimStart.get_value();
                waveformArea.queue_draw();
                std::ostringstream ph;
                ph << std::fixed << std::setprecision(2) << playheadSeconds;
                waveformHelp.set_text("Waveform: preview returned to trim start (" + ph.str() + " s)");
                return true;
            }

            std::optional<double> pos = previewPos;
            if (!pos) pos = audio.position_for_key(id);
            if (!pos) return true;

            double t = std::max(0.0, std::min(audioDuration, *pos));
            if (std::abs(t - playheadSeconds) >= 0.015) {
                playheadSeconds = t;
                waveformArea.queue_draw();
                std::ostringstream ph;
                ph << std::fixed << std::setprecision(2) << playheadSeconds;
                waveformHelp.set_text("Waveform ready: drag handles to trim, click waveform to preview/playhead (" + ph.str() + " s).");
            }
            return true;
        }, 50);

        reloadWaveform();
        auto* trimControlsRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        trimControlsRow->set_valign(Gtk::Align::CENTER);
        trimControlsRow->append(trimStartLabel );
        trimControlsRow->append(trimStart );
        trimControlsRow->append(trimEndLabel );
        trimControlsRow->append(trimEnd );
        trimControlsRow->append(resetTrimButton );
        trimControlsRow->append(setStartButton );
        trimControlsRow->append(setEndButton );
        trimControlsRow->append(playPreviewButton );
        trimControlsRow->append(stopPreviewButton );

        trimBox->append(durationLabel );
        trimBox->append(waveformArea );
        trimBox->append(waveformHelp );
        trimBox->append(*trimControlsRow );

        auto* statusBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        ::set_margin(*statusBox, 10);
        Gtk::Label statusInfo;
        statusInfo.set_xalign(0.0f);
        statusInfo.set_wrap(true);
        statusBox->append(statusInfo );

        auto updateLabels = [&]() {
            volLabel.set_text("Volume: " + format_db(static_cast<float>(vol.get_value())));
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << speed.get_value();
            speedLabel.set_text("Speed: " + ss.str() + "x");
            std::ostringstream ps;
            ps << std::fixed << std::setprecision(2) << pan.get_value();
            std::string side = "Center";
            if (pan.get_value() < -0.01) side = "Left";
            else if (pan.get_value() > 0.01) side = "Right";
            panLabel.set_text("Pan: " + ps.str() + " (" + side + ")");
            fadeInLabel.set_text("Fade in: " + std::to_string(static_cast<int>(fadeIn.get_value())) + " ms");
            fadeOutLabel.set_text("Fade out: " + std::to_string(static_cast<int>(fadeOut.get_value())) + " ms");
            std::ostringstream ns;
            const auto activeNorm = normalizationCombo.get_active_id();
            NormalizationMode visibleMode = normalization_mode_from_string(activeNorm.empty() ? "Off" : activeNorm);
            ns << "Mode: " << display_label(visibleMode) << "\n";
            if (b.analysisValid) {
                ns << std::fixed << std::setprecision(2)
                   << "Measured LUFS: " << b.measuredLufs << "\n"
                   << "Sample peak: " << b.measuredPeakDb << " dBFS\n"
                   << "Auto gain: " << b.normalizationGainDb << " dB\n";
            } else {
                ns << "Measured LUFS: pending\nSample peak: pending\nAuto gain: 0 dB\n";
            }
            ns << "Analysis status: " << (b.analysisFailed ? "failed" : (b.analysisValid ? "valid" : "pending"));
            normalizationInfo.set_text(ns.str());
            hotkeyLabel.set_text("Hotkey: " + (learnedHotkeyKeyval ? learnedHotkeyLabel : std::string("None")));
            midiLabel.set_text("MIDI: " + format_midi_trigger(learnedMidiChannel, learnedMidiNote));

            std::ostringstream ts, te;
            ts << std::fixed << std::setprecision(2) << trimStart.get_value();
            te << std::fixed << std::setprecision(2) << trimEnd.get_value();
            if (audioDuration > 0.0) {
                std::ostringstream ds;
                ds << std::fixed << std::setprecision(2) << audioDuration;
                durationLabel.set_text("Duration: " + ds.str() + " s");
                trimStartLabel.set_text("Start");
                trimEndLabel.set_text("End");
                if (!waveformBusy && !waveformPeaks.empty()) {
                    std::ostringstream ph;
                    ph << std::fixed << std::setprecision(2) << playheadSeconds;
                    waveformHelp.set_text(trim_editor_reversed()
                                              ? "Waveform ready: reversed playback order; drag handles to trim, click waveform to preview/playhead (" + ph.str() + " s)."
                                              : "Waveform ready: drag handles to trim, click waveform to preview/playhead (" + ph.str() + " s).");
                }
            } else {
                durationLabel.set_text(b.assigned ? um::duration_unknown(ffprobeAvailable) : "No audio loaded yet.");
                trimStartLabel.set_text("Start");
                trimEndLabel.set_text("End");
                waveformHelp.set_text("No audio loaded. Use Load Audio on the Pad tab first.");
            }

            waveformArea.queue_draw();

            std::string reverseStatus = "Not needed";
            if (b.assigned) {
                if (repository.reverse_audio_is_ready(b)) reverseStatus = "Ready: reverse playback available";
                else if (reverse_job_running(id, b.storedFilename)) reverseStatus = "Generating reverse audio...";
                else if (reverseErrors.find(id) != reverseErrors.end()) reverseStatus = "Failed: retry by playing reverse again";
                else if (b.playbackDirection == PlaybackDirection::Reverse) reverseStatus = ffmpegAvailable ? "Will generate on next play" : "ffmpeg missing";
                else reverseStatus = ffmpegAvailable ? "Will generate if Reverse is selected" : "ffmpeg missing";
            }
            statusInfo.set_text("Reverse cache: " + reverseStatus);
        };
        updateLabels();
        setWaveformBusy();

        dialog.set_default_size(760, 560);

        auto* notebook = Gtk::make_managed<Gtk::Notebook>();
        notebook->set_scrollable(false);

        auto* padTab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        ::set_margin(*padTab, 10);
        padTab->append(*make_section("Pad", *padBox) );
        padTab->append(*make_section("Trigger", *triggerBox) );

        auto* playbackTab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        ::set_margin(*playbackTab, 10);
        playbackTab->append(*make_section("Playback", *playbackBox) );

        auto* trimTab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        ::set_margin(*trimTab, 10);
        trimTab->append(*make_section("Trim", *trimBox) );

        auto* loudnessTab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        ::set_margin(*loudnessTab, 10);
        loudnessTab->append(*make_section("Loudness Normalization", *loudnessBox) );

        auto* advancedTab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        ::set_margin(*advancedTab, 10);
        advancedTab->append(*make_section("Status", *statusBox) );

        notebook->append_page(*padTab, "Pad");
        notebook->append_page(*playbackTab, "Playback");
        notebook->append_page(*trimTab, "Trim");
        notebook->append_page(*loudnessTab, "Loudness");
        notebook->append_page(*advancedTab, "Advanced");
        box->append(*notebook );

        auto commit_label = [&]() {
            auto trimmed = trim_copy(labelEntry.get_text());

            if (trimmed == b.label) return;

            if (trimmed.empty()) {
                b.label = repository.default_label_for(b);
                b.userRenamed = false;
            } else {
                b.label = trimmed;
                b.userRenamed = true;
            }
        };

        TaskHandle normalizationTask;
        auto ensureNormalizationAnalysis = [&]() {
            auto normId = normalizationCombo.get_active_id();
            if (normId == "trimmed") b.normalizationMode = NormalizationMode::TrimmedRegion;
            else b.normalizationMode = NormalizationMode::Off;

            b.trimStart = clamp_time_seconds(trimStart.get_value());
            b.trimEnd = clamp_time_seconds(trimEnd.get_value());
            if (b.assigned && b.durationSeconds > 0.0) {
                b.trimStart = std::min(b.trimStart, b.durationSeconds);
                b.trimEnd = std::min(b.trimEnd, b.durationSeconds);
                if (b.trimEnd <= b.trimStart) { b.trimStart = 0.0; b.trimEnd = b.durationSeconds; }
            }

            if (b.normalizationMode == NormalizationMode::Off) {
                invalidate_normalization_analysis(b);
                updateLabels();
                return;
            }
            if (!b.assigned || b.storedFilename.empty()) {
                b.analysisValid = false;
                b.analysisFailed = true;
                updateLabels();
                return;
            }
            const auto file = repository.sound_file(b);
            const auto region = normalization_analysis_region(b);
            const auto timestamp = file_timestamp_for_analysis(file);
            const bool cacheMatches = b.analysisValid && !b.analysisFailed &&
                b.analysisSourceFile == file.string() && b.analysisSourceTimestamp == timestamp &&
                std::abs(b.analysisRegionStart - region.first) <= 0.0001 &&
                std::abs(b.analysisRegionEnd - region.second) <= 0.0001;
            if (cacheMatches) {
                updateLabels();
                return;
            }

            if (normalizationTask) normalizationTask.cancel();
            b.analysisValid = false;
            b.analysisFailed = false;
            b.analysisRegionStart = region.first;
            b.analysisRegionEnd = region.second;
            b.analysisSourceFile = file.string();
            b.analysisSourceTimestamp = timestamp;
            b.measuredLufs = 0.0;
            b.measuredPeakDb = 0.0;
            b.normalizationGainDb = 0.0;
            updateLabels();

            auto alive = windowAlive;
            normalizationTask = taskRunner.submit(
                "loudness-analysis",
                [file, start = region.first, end = region.second](pulsepad::CancellationToken token) {
                    if (token.cancellation_requested()) return pulsepad::LoudnessAnalysisResult{};
                    return analyze_loudness_with_ffmpeg(file, start, end);
                },
                [this, alive, dialogAlive, &b, fileString = file.string(), timestamp, start = region.first, end = region.second, &updateLabels](TaskOutcome<pulsepad::LoudnessAnalysisResult> outcome) mutable {
                    if (!alive->load() || !dialogAlive->load()) return;
                    if (outcome.status == TaskStatus::Cancelled) return;
                    if (b.analysisSourceFile != fileString || b.analysisSourceTimestamp != timestamp ||
                        std::abs(b.analysisRegionStart - start) > 0.0001 || std::abs(b.analysisRegionEnd - end) > 0.0001) return;
                    if (!outcome.succeeded() || !outcome.value.ok) {
                        b.analysisValid = false;
                        b.analysisFailed = true;
                        updateLabels();
                        return;
                    }
                    b.measuredLufs = outcome.value.measuredLufs;
                    b.measuredPeakDb = outcome.value.measuredPeakDb;
                    b.normalizationGainDb = outcome.value.normalizationGainDb;
                    b.analysisValid = true;
                    b.analysisFailed = false;
                    updateLabels();
                });
        };

        auto apply_controls_to_button = [&]() {
            auto modeId = modeCombo.get_active_id();
            if (modeId == "loop") b.playbackMode = PlaybackMode::Loop;
            else if (modeId == "retrigger") b.playbackMode = PlaybackMode::Retrigger;
            else b.playbackMode = PlaybackMode::PlayThrough;

            b.playbackDirection = directionCombo.get_active_id() == "reverse" ? PlaybackDirection::Reverse : PlaybackDirection::Forward;
            b.color = normalize_pad_color(colorCombo.get_active_id());
            b.volume = db_to_linear_volume(static_cast<float>(vol.get_value()));
            b.playbackSpeed = clamp_playback_speed(static_cast<float>(speed.get_value()));
            b.pan = clamp_pan(static_cast<float>(pan.get_value()));
            b.fadeInMs = clamp_fade_ms(static_cast<int>(fadeIn.get_value()));
            b.fadeOutMs = clamp_fade_ms(static_cast<int>(fadeOut.get_value()));
            const auto previousMode = b.normalizationMode;
            auto normId = normalizationCombo.get_active_id();
            if (normId == "trimmed") b.normalizationMode = NormalizationMode::TrimmedRegion;
            else b.normalizationMode = NormalizationMode::Off;
            if (b.normalizationMode != previousMode) invalidate_normalization_analysis(b);
            b.exclusiveGroup = normalize_group_name(groupCombo.get_active_id());
            b.hotkeyKeyval = learnedHotkeyKeyval;
            b.hotkeyLabel = learnedHotkeyLabel;
            b.midiChannel = learnedMidiChannel;
            b.midiNote = learnedMidiNote;
            const double previousTrimStart = b.trimStart;
            const double previousTrimEnd = b.trimEnd;
            b.trimStart = clamp_time_seconds(trimStart.get_value());
            b.trimEnd = clamp_time_seconds(trimEnd.get_value());
            if (b.assigned && b.durationSeconds > 0.0) {
                b.trimStart = std::min(b.trimStart, b.durationSeconds);
                b.trimEnd = std::min(b.trimEnd, b.durationSeconds);
                if (b.trimEnd <= b.trimStart) { b.trimStart = 0.0; b.trimEnd = b.durationSeconds; }
            }
            if (std::abs(previousTrimStart - b.trimStart) > 0.0001 || std::abs(previousTrimEnd - b.trimEnd) > 0.0001) invalidate_normalization_analysis(b);
        };

        loadButton.signal_clicked().connect([&, id]() {
            cancel_reverse_job(id);
            commit_label();
            apply_controls_to_button();
            Gtk::FileChooserDialog chooser(*this, "Load Audio", Gtk::FileChooser::Action::OPEN);
            chooser.add_button("Cancel", Gtk::ResponseType::CANCEL);
            chooser.add_button("Open", Gtk::ResponseType::OK);
            auto filter = Gtk::FileFilter::create();
            filter->set_name("Audio files");
            filter->add_pattern("*.mp3"); filter->add_pattern("*.wav"); filter->add_pattern("*.ogg"); filter->add_pattern("*.flac"); filter->add_pattern("*.m4a");
            chooser.add_filter(filter);
            if (!fileDialogMemory.lastAudioImportDir.empty()) chooser.set_current_folder(Gio::File::create_for_path(fileDialogMemory.lastAudioImportDir));
            if (run_dialog_blocking(chooser) == Gtk::ResponseType::OK) {
                const auto selectedFile = chooser.get_file()->get_path();
                remember_file_dialog_folder(fileDialogMemory.lastAudioImportDir, selectedFile);
                SoundButton draft = b;
                SoundButton previousDraft = b;
                const auto previousLoadLabel = loadButton.get_label();
                loadButton.set_sensitive(false);
                loadButton.set_label("Loading audio...");
                set_status("Importing audio...");
                const auto rootDir = repository.root_dir();
                auto alive = windowAlive;
                taskRunner.submit(
                    "audio-import",
                    [rootDir, selectedFile, draft](pulsepad::CancellationToken token) {
                        if (token.cancellation_requested()) return draft;
                        BoardRepository workerRepository(rootDir);
                        return workerRepository.import_audio_for_button(draft, selectedFile);
                    },
                    [this, alive, dialogAlive, rootDir, previousDraft, originalStored = originalButton.storedFilename,
                     previousLoadLabel, &b, &audioDuration, &trimMax, &trimStart, &trimEnd, &playheadSeconds, &labelEntry,
                     &loadButton, &reloadWaveform, &updateLabels, &ensureNormalizationAnalysis](TaskOutcome<SoundButton> outcome) mutable {
                        if (!alive->load()) return;
                        if (!dialogAlive->load()) {
                            if (outcome.succeeded()) BoardRepository(rootDir).remove_audio_assets(outcome.value);
                            return;
                        }
                        loadButton.set_sensitive(true);
                        loadButton.set_label(previousLoadLabel);
                        if (outcome.status == TaskStatus::Cancelled) {
                            set_status("Audio import cancelled");
                            return;
                        }
                        if (!outcome.succeeded()) {
                            set_status(outcome.userMessage);
                            return;
                        }
                        SoundButton imported = std::move(outcome.value);
                        if (previousDraft.storedFilename != originalStored && previousDraft.storedFilename != imported.storedFilename) {
                            BoardRepository(rootDir).remove_audio_assets(previousDraft);
                        }
                        b = std::move(imported);
                        invalidate_normalization_analysis(b);
                        audioDuration = b.durationSeconds;
                        trimMax = audioDuration > 0.0 ? audioDuration : 300.0;
                        trimStart.set_range(0, trimMax);
                        trimEnd.set_range(0, trimMax);
                        trimStart.set_value(0.0);
                        trimEnd.set_value(audioDuration > 0.0 ? audioDuration : 0.0);
                        playheadSeconds = 0.0;
                        reloadWaveform();
                        labelEntry.set_text(b.label);
                        loadButton.set_label("Audio File: " + b.originalFilename);
                        updateLabels();
                        ensureNormalizationAnalysis();
                        set_status("Audio loaded; save the dialog to keep it");
                    });
            } else set_status("File selection cancelled");
        });

        clearButton.signal_clicked().connect([&, id]() {
            cancel_reverse_job(id);
            if (b.storedFilename != originalButton.storedFilename) repository.remove_audio_assets(b);
            b = default_button(id);
            invalidate_normalization_analysis(b);
            normalizationCombo.set_active_id("trimmed");
            labelEntry.set_text(b.label);
            loadButton.set_label("Choose Audio File");
            audioDuration = 0.0;
            trimStart.set_range(0, 300.0);
            trimEnd.set_range(0, 300.0);
            trimStart.set_value(0.0);
            trimEnd.set_value(0.0);
            playheadSeconds = 0.0;
            reloadWaveform();
            updateLabels();
            set_status("Audio cleared; save the dialog to keep it");
        });

        resetLabelButton.signal_clicked().connect([&]() {
            b.label = repository.default_label_for(b);
            b.userRenamed = false;
            labelEntry.set_text(b.label);
            updateLabels();
        });

        modeCombo.signal_changed().connect([&]() {
            updateLabels();
        });

        directionCombo.signal_changed().connect([&]() {
            if (audioDuration > 0.0) {
                auto [displayStart, unusedDisplayEnd] = display_trim_range();
                (void)unusedDisplayEnd;
                playheadSeconds = displayStart;
            }
            updateLabels();
        });

        learnHotkeyButton.signal_clicked().connect([&]() {
            learningHotkey = true;
            learnHotkeyButton.set_label("Press a key...");
            hotkeyLabel.set_text("Hotkey: press a key now");
            dialog.grab_focus();
        });
        clearHotkeyButton.signal_clicked().connect([&]() {
            learningHotkey = false;
            learnedHotkeyKeyval = 0;
            learnedHotkeyLabel.clear();
            learnHotkeyButton.set_label("Learn Hotkey");
            updateLabels();
        });
        learnMidiButton.signal_clicked().connect([&]() {
            learnMidiButton.set_label("Press MIDI note...");
            midiLabel.set_text("MIDI: press a MIDI note now");
            midiLearnCallback = [&](int channel, int note, int velocity) {
                if (velocity <= 0) return;
                learnedMidiChannel = channel;
                learnedMidiNote = note;
                learnMidiButton.set_label("Learn MIDI");
                midiLearnCallback = nullptr;
                updateLabels();
            };
        });
        clearMidiButton.signal_clicked().connect([&]() {
            learnedMidiChannel = -1;
            learnedMidiNote = -1;
            learnMidiButton.set_label("Learn MIDI");
            midiLearnCallback = nullptr;
            updateLabels();
        });
        auto dialogKeyController = Gtk::EventControllerKey::create();
        dialogKeyController->signal_key_pressed().connect([&](guint keyval, guint, Gdk::ModifierType) {
            if (!learningHotkey) return false;
            learnedHotkeyKeyval = keyval;
            const char* keyName = gdk_keyval_name(keyval);
            learnedHotkeyLabel = keyName ? keyName : std::to_string(keyval);
            learningHotkey = false;
            learnHotkeyButton.set_label("Learn Hotkey");
            updateLabels();
            return true;
        }, false);
        dialog.add_controller(dialogKeyController);

        vol.signal_value_changed().connect([&]() {
            audio.set_button_volume(id, db_to_linear_volume(static_cast<float>(vol.get_value())));
            updateLabels();
        });
        speed.signal_value_changed().connect([&]() {
            audio.set_playback_speed(id, clamp_playback_speed(static_cast<float>(speed.get_value())));
            updateLabels();
        });
        pan.signal_value_changed().connect([&]() {
            audio.set_button_pan(id, clamp_pan(static_cast<float>(pan.get_value())));
            updateLabels();
        });
        fadeIn.signal_value_changed().connect([&]() { updateLabels(); });
        fadeOut.signal_value_changed().connect([&]() { updateLabels(); });
        normalizationCombo.signal_changed().connect([&]() { ensureNormalizationAnalysis(); });
        groupCombo.signal_changed().connect([&]() { updateLabels(); });
        trimStart.signal_value_changed().connect([&]() {
            double v = clamp_time_seconds(trimStart.get_value());
            if (audioDuration > 0.0) v = std::min(v, audioDuration);
            double minGap = audioDuration > 0.0 ? std::min(0.01, audioDuration) : 0.0;
            if (audioDuration > 0.0 && v >= trimEnd.get_value()) {
                v = std::max(0.0, trimEnd.get_value() - minGap);
                trimStart.set_value(v);
            }
            invalidate_normalization_analysis(b);
            ensureNormalizationAnalysis();
        });
        trimEnd.signal_value_changed().connect([&]() {
            double v = clamp_time_seconds(trimEnd.get_value());
            if (audioDuration > 0.0) v = std::min(v, audioDuration);
            double minGap = audioDuration > 0.0 ? std::min(0.01, audioDuration) : 0.0;
            if (audioDuration > 0.0 && v <= trimStart.get_value()) {
                v = std::min(audioDuration, trimStart.get_value() + minGap);
                trimEnd.set_value(v);
            }
            invalidate_normalization_analysis(b);
            ensureNormalizationAnalysis();
        });
        resetTrimButton.signal_clicked().connect([&]() {
            trimStart.set_value(0.0);
            trimEnd.set_value(audioDuration > 0.0 ? audioDuration : 0.0);
            playheadSeconds = 0.0;
            invalidate_normalization_analysis(b);
            ensureNormalizationAnalysis();
        });
        setStartButton.signal_clicked().connect([&]() {
            if (audioDuration <= 0.0) return;
            double minGap = std::min(0.01, audioDuration);
            if (trim_editor_reversed()) {
                double sourceEnd = std::max(trimStart.get_value() + minGap, audioDuration - playheadSeconds);
                trimEnd.set_value(std::min(audioDuration, sourceEnd));
            } else {
                trimStart.set_value(std::min(playheadSeconds, trimEnd.get_value() - minGap));
            }
            invalidate_normalization_analysis(b);
            ensureNormalizationAnalysis();
        });
        setEndButton.signal_clicked().connect([&]() {
            if (audioDuration <= 0.0) return;
            double minGap = std::min(0.01, audioDuration);
            if (trim_editor_reversed()) {
                double sourceStart = std::min(trimEnd.get_value() - minGap, audioDuration - playheadSeconds);
                trimStart.set_value(std::max(0.0, sourceStart));
            } else {
                trimEnd.set_value(std::max(playheadSeconds, trimStart.get_value() + minGap));
            }
            invalidate_normalization_analysis(b);
            ensureNormalizationAnalysis();
        });
        playPreviewButton.signal_clicked().connect([&, previewKey]() {
            if (!b.assigned || b.storedFilename.empty()) { set_status("No audio assigned. Load audio before previewing trim."); return; }
            try {
                audio.stop_key_immediate(previewKey);
                previewActive = false;
                const bool reversePreview = trim_editor_reversed();
                fs::path file = repository.sound_file(b);
                if (reversePreview) {
                    if (!repository.reverse_audio_is_ready(b)) {
                        start_reverse_prepare_job(id, b, "Generating reverse audio before trim preview...", false);
                        set_status("Reverse audio is not ready yet. Try preview again after generation finishes.");
                        return;
                    }
                    file = repository.reverse_sound_file(b);
                }
                double start = audioDuration > 0.0 ? trimStart.get_value() : 0.0;
                double end = trimEnd.get_value();
                if (audioDuration > 0.0) {
                    start = std::max(0.0, std::min(audioDuration, start));
                    end = std::max(0.0, std::min(audioDuration, end));
                    if (end <= start) {
                        start = 0.0;
                        end = audioDuration;
                    }
                    if (reversePreview) {
                        const double sourceStart = start;
                        const double sourceEnd = end;
                        start = std::max(0.0, audioDuration - sourceEnd);
                        end = std::min(audioDuration, audioDuration - sourceStart);
                    }
                    playheadSeconds = start;
                    previewActive = true;
                    waveformArea.queue_draw();
                    std::ostringstream ph;
                    ph << std::fixed << std::setprecision(2) << playheadSeconds;
                    waveformHelp.set_text(reversePreview ? "Waveform: reverse preview from trim start (" + ph.str() + " s)"
                                                         : "Waveform: preview from trim start (" + ph.str() + " s)");
                }
                if (audioDuration > 0.0 && std::abs(end - audioDuration) <= 0.01) end = 0.0;
                if (audioDuration <= 0.0) previewActive = true;
                audio.play(previewKey, file, db_to_linear_volume(static_cast<float>(vol.get_value())), 1.0f, clamp_playback_speed(static_cast<float>(speed.get_value())), 1.0f,
                           PlaybackMode::Retrigger, clamp_pan(static_cast<float>(pan.get_value())), std::string(), start, end, 0, 0,
                           [this](const std::string& msg) { Glib::signal_idle().connect_once([this, msg]() { set_status(msg); }); });
                refresh_mixer_ui();
                set_status("Preview playback started");
            } catch (const std::exception& ex) { set_status(ex.what()); }
        });
        stopPreviewButton.signal_clicked().connect([&, previewKey]() {
            audio.stop_key_immediate(previewKey);
            previewActive = false;
            if (audioDuration > 0.0) {
                playheadSeconds = trimStart.get_value();
                waveformArea.queue_draw();
                std::ostringstream ph;
                ph << std::fixed << std::setprecision(2) << playheadSeconds;
                waveformHelp.set_text("Waveform: preview stopped at trim start (" + ph.str() + " s)");
            }
            refresh_mixer_ui();
            set_status("Preview stopped");
        });

        ensureNormalizationAnalysis();

        int response = run_dialog_blocking(dialog);
        dialogAlive->store(false);
        if (normalizationTask) normalizationTask.cancel();
        *waveformTimerAlive = false;
        if (*waveformTask) waveformTask->cancel();
        previewActive = false;
        audio.stop_key_immediate(previewKey);
        midiLearnCallback = nullptr;
        if (response == Gtk::ResponseType::OK) {
            if (originalButton.storedFilename != b.storedFilename) { cancel_reverse_job(id); reverseErrors.erase(id); set_status_light(um::reverse_invalidated()); }
            commit_label();
            apply_controls_to_button();
            if (id >= 0 && id < static_cast<int>(state.buttons.size())) state.buttons[checked_index(id)] = b;
            if (originalButton.storedFilename != b.storedFilename) repository.remove_audio_assets(originalButton);
            state.status = "Button updated";
            repository.save(state);
            refresh_ui();
            if (b.playbackDirection != PlaybackDirection::Reverse) reverseErrors.erase(id);
            if (b.assigned && b.playbackDirection == PlaybackDirection::Reverse) ensure_reverse_ready_or_start(id);
        } else {
            if (b.storedFilename != originalButton.storedFilename) { cancel_reverse_job(id); reverseErrors.erase(id); repository.remove_audio_assets(b); }
            refresh_ui();
        }
    }

    void on_export_board() {
        auto path = ui::choose_export_path(*this, "board.pad", fileDialogMemory.lastBoardSaveDir);
        if (!path) { set_status("Export cancelled"); return; }
        remember_file_dialog_folder(fileDialogMemory.lastBoardSaveDir, *path);
        const BoardState snapshot = state;
        boardExportInProgress = true;
        set_status("Exporting board...");
        const auto rootDir = repository.root_dir();
        auto alive = windowAlive;
        taskRunner.submit(
            "board-export",
            [path = *path, snapshot, rootDir](pulsepad::CancellationToken token) {
                if (token.cancellation_requested()) return false;
                BoardRepository workerRepository(rootDir);
                BoardPackage workerPackage(workerRepository);
                workerPackage.export_to(path, snapshot);
                return true;
            },
            [this, alive](TaskOutcome<bool> outcome) {
                if (!alive->load()) return;
                boardExportInProgress = false;
                if (outcome.status == TaskStatus::Cancelled) set_status("Export cancelled");
                else if (outcome.succeeded()) set_status("Export successful");
                else set_status(um::board_export_failed(outcome.userMessage));
            });
    }

    void on_import_board() {
        auto path = ui::choose_import_path(*this, fileDialogMemory.lastBoardLoadDir);
        if (!path) { set_status("Import cancelled"); return; }
        remember_file_dialog_folder(fileDialogMemory.lastBoardLoadDir, *path);
        if (!ui::confirm_replace_board(*this)) return;
        boardImportInProgress = true;
        set_status("Importing board...");
        const auto rootDir = repository.root_dir();
        auto alive = windowAlive;
        taskRunner.submit(
            "board-import",
            [path = *path, rootDir](pulsepad::CancellationToken token) {
                if (token.cancellation_requested()) return BoardState{};
                BoardRepository workerRepository(rootDir);
                BoardPackage workerPackage(workerRepository);
                return workerPackage.import_from(path);
            },
            [this, alive](TaskOutcome<BoardState> outcome) mutable {
                if (!alive->load()) return;
                boardImportInProgress = false;
                if (outcome.status == TaskStatus::Cancelled) {
                    set_status("Import cancelled");
                    return;
                }
                if (!outcome.succeeded()) {
                    set_status(um::board_import_failed(outcome.userMessage));
                    return;
                }
                state = std::move(outcome.value);
                audio.set_master_volume(db_to_linear_volume(state.masterVolumeDb));
                state.status = "Board loaded";
                refresh_ui();
            });
    }
};

#include "PulsePadWindow.h"

int run_pulsepad_app(int argc, char** argv) {
    Gio::init();
    auto app = Gtk::Application::create("org.pulsepad.gtk");
    return app->make_window_and_run<PulsePadWindow>(argc, argv);
}
