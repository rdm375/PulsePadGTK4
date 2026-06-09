#include "UiDialogs.h"

namespace pulsepad::ui {

namespace {

int run_dialog_blocking(Gtk::Dialog& dialog) {
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

} // namespace

void setup_icon_button(Gtk::Button& button, const std::string& iconName, const std::string& tooltip) {
    auto* image = Gtk::make_managed<Gtk::Image>();
    image->set_from_icon_name(iconName);
    image->set_pixel_size(32);
    button.set_child(*image);
    button.set_tooltip_text(tooltip);
    button.set_size_request(72, 64);
}

std::optional<std::string> choose_export_path(Gtk::Window& parent, const std::string& defaultName, const std::string& initialFolder) {
    Gtk::FileChooserDialog chooser(parent, "Save Board", Gtk::FileChooser::Action::SAVE);
    chooser.add_button("Cancel", Gtk::ResponseType::CANCEL);
    chooser.add_button("Save", Gtk::ResponseType::OK);
    if (!initialFolder.empty()) chooser.set_current_folder(Gio::File::create_for_path(initialFolder));
    chooser.set_current_name(defaultName);
    if (run_dialog_blocking(chooser) != Gtk::ResponseType::OK) return std::nullopt;
    return chooser.get_file()->get_path();
}

std::optional<std::string> choose_import_path(Gtk::Window& parent, const std::string& initialFolder) {
    Gtk::FileChooserDialog chooser(parent, "Load Board", Gtk::FileChooser::Action::OPEN);
    chooser.add_button("Cancel", Gtk::ResponseType::CANCEL);
    chooser.add_button("Open", Gtk::ResponseType::OK);
    if (!initialFolder.empty()) chooser.set_current_folder(Gio::File::create_for_path(initialFolder));
    if (run_dialog_blocking(chooser) != Gtk::ResponseType::OK) return std::nullopt;
    return chooser.get_file()->get_path();
}

bool confirm_replace_board(Gtk::Window& parent) {
    Gtk::MessageDialog confirm(parent, "Replace current board?", false, Gtk::MessageType::WARNING, Gtk::ButtonsType::OK_CANCEL, true);
    confirm.set_secondary_text("Loading a board replaces the current board. Save first if you want a backup.");
    return run_dialog_blocking(confirm) == Gtk::ResponseType::OK;
}

void show_error(Gtk::Window& parent, const std::string& message) {
    Gtk::MessageDialog dialog(parent, message, false, Gtk::MessageType::ERROR, Gtk::ButtonsType::OK, true);
    run_dialog_blocking(dialog);
}

} // namespace pulsepad::ui
