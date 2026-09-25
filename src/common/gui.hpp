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
	std::function<void(float)> scale_changed; // a new UI scale is in use (also the first one)
};

// Opens the window and runs until it is closed. Returns false if it could not open.
// width / height are for a UI scale of 1.
bool run_gui(const char *title, int width, int height, const GuiCallbacks &cb);

// ------------------------------------------------------------ UI scale ---
// How big everything is drawn: 1 fits a 1920x1080 screen, 2 a 4K one.

extern const float kUiScales[]; // the choices offered in menus
extern const int kUiScaleCount;

float ui_scale();
void set_ui_scale(float scale); // from the next frame on; 0 = the one that fits the screen
float screen_ui_scale();        // the one that fits the screen the window is on
void step_ui_scale(int dir);    // the next bigger (+1) / smaller (-1) choice
bool ui_scale_shortcuts();      // Ctrl + / Ctrl - / Ctrl 0 (fit screen); true = one was pressed

inline float px(float size) { return size * ui_scale(); } // a size in pixels at scale 1 -> now

// ------------------------------------------------------------- widgets ---

inline ImVec4 to_imvec(Color c) { return ImVec4(c.r, c.g, c.b, c.a); }

bool text_field(const char *id, std::string &s, float width, bool multiline = false);
void status_dot(bool on);

// Label on the left, the next widget aligned in a column after it.
inline float label_width() { return px(118); }
void row_label(const char *text);
