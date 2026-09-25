// SDL2 + Dear ImGui window with a main loop, shared by both programs.
#pragma once

#include "blocks.hpp"
#include "imgui.h"

#include <functional>
#include <string>

struct GuiCallbacks {
	std::function<void()> update;  // called every loop, before drawing
	std::function<void()> draw;    // ImGui drawing
	std::function<bool()> busy;    // true = wake up often (something is running)
	std::function<std::string()> title; // window title, updated when it changes (default: the one given)
	std::function<bool()> close_requested; // the user closes the window: true = close now, false = not yet
	std::function<bool()> should_quit;     // true = close the window now (polled every loop)
};

// Opens the window and runs until it is closed. Returns false if it could not open.
bool run_gui(const char *title, int width, int height, const GuiCallbacks &cb);

// ------------------------------------------------------------- widgets ---

inline ImVec4 to_imvec(Color c) { return ImVec4(c.r, c.g, c.b, c.a); }

bool text_field(const char *id, std::string &s, float width, bool multiline = false);
void status_dot(bool on);

// Label on the left, the next widget aligned in a column after it.
constexpr float kLabelWidth = 118;
void row_label(const char *text);
