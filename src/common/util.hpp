// Small string / path helpers.
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

std::string home();    // $HOME, or %USERPROFILE% on Windows
std::string exe_dir(); // folder of the running program

// Linux: $XDG_CONFIG_HOME/autom8, or ~/.config/autom8. Windows: %APPDATA%\autom8
std::string config_dir();

// The program's arguments as UTF-8 (on Windows, from the UTF-16 command line), argv[0] included.
std::vector<std::string> utf8_args(int argc, char **argv);

// The sequence file to use: `given` (made absolute, ~ and $VARS expanded) if not empty,
// else sequences.json next to the program if there is one, else sequences.json in config_dir().
std::string document_path(const std::string &given);

// UTF-8 string <-> filesystem path (plain on Linux; UTF-16 underneath on Windows).
std::filesystem::path path_of(const std::string &utf8);
std::string path_string(const std::filesystem::path &p);

bool get_env(const std::string &name, std::string &value); // false if not set

inline double secs_since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

std::string trim(std::string s);

// "a, b,,c" -> {"a", "b", "c"}
std::vector<std::string> split_list(const std::string &s, char sep);

// Expands a leading ~ and $VAR / ${VAR} from the environment.
std::string expand(const std::string &in);

// Splits a command line into arguments, honouring "double" and 'single' quotes.
std::vector<std::string> split_command(const std::string &s);

#ifdef _WIN32
// Every std::string in AutoM8 is UTF-8; Windows APIs want UTF-16.
std::wstring widen(const std::string &s);
std::string narrow(const std::wstring &s);
#endif
