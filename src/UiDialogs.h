#pragma once

#include <gtkmm.h>

#include <optional>
#include <string>

namespace pulsepad::ui {

void setup_icon_button(Gtk::Button& button, const std::string& iconName, const std::string& tooltip);
std::optional<std::string> choose_export_path(Gtk::Window& parent, const std::string& defaultName, const std::string& initialFolder = {});
std::optional<std::string> choose_import_path(Gtk::Window& parent, const std::string& initialFolder = {});
bool confirm_replace_board(Gtk::Window& parent);
void show_error(Gtk::Window& parent, const std::string& message);

} // namespace pulsepad::ui
