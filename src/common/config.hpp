// User settings shared by autom8 and autom8-editor, in config_dir()/config.json
// (Linux: ~/.config/autom8/config.json, Windows: %APPDATA%\autom8\config.json).
#pragma once

#include <string>

constexpr int kConfigVersion = 1; // "version" in the file; a newer file is read but never written over

struct Config {
	float ui_scale = 0;    // size of the interface, 1 = made for 1920x1080; 0 = not chosen yet
	std::string last_file; // the editor opens it again when no file is given
};

std::string config_path();

// Defaults for a missing or unreadable file.
Config load_config(const std::string &path = config_path());

// Each one changes a single setting in the file as it is now on disk, so the two programs
// don't undo each other's changes. false: could not write (or the file is from a newer version).
bool save_ui_scale(float scale, const std::string &path = config_path());
bool save_last_file(const std::string &file, const std::string &path = config_path());
