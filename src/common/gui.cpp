#include "gui.hpp"

#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "misc/cpp/imgui_stdlib.h"

#define SDL_MAIN_HANDLED // we have our own main()
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ------------------------------------------------------------ UI scale ---

const float kUiScales[] = {1, 1.25f, 1.5f, 1.75f, 2, 2.5f, 3};
const int kUiScaleCount = sizeof kUiScales / sizeof kUiScales[0];

static float g_scale = 0;           // asked for (0 = fit the screen)
static float g_applied = 0;         // what the fonts and style are built for
static SDL_Window *g_window = nullptr;

float ui_scale() { return g_applied > 0 ? g_applied : 1; }
void set_ui_scale(float scale) { g_scale = scale > 0 ? std::clamp(scale, 0.5f, 4.0f) : 0; }

float screen_ui_scale()
{
	int display = g_window ? std::max(0, SDL_GetWindowDisplayIndex(g_window)) : 0;
	SDL_DisplayMode mode;
	if (SDL_GetDesktopDisplayMode(display, &mode) != 0) return 1;
	float fit = std::min(mode.w / 1920.0f, mode.h / 1080.0f); // 1080p -> 1, 1440p -> 1.33, 4K -> 2
	return std::clamp(std::round(fit * 4) / 4, 1.0f, 3.0f);   // to the nearest quarter
}

void step_ui_scale(int dir)
{
	float now = ui_scale();
	if (dir > 0) {
		for (int i = 0; i < kUiScaleCount; i++)
			if (kUiScales[i] > now + 0.01f) return set_ui_scale(kUiScales[i]);
	} else {
		for (int i = kUiScaleCount - 1; i >= 0; i--)
			if (kUiScales[i] < now - 0.01f) return set_ui_scale(kUiScales[i]);
	}
}

bool ui_scale_shortcuts()
{
	auto key = [](ImGuiKeyChord chord) { return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal); };
	if (key(ImGuiMod_Ctrl | ImGuiKey_Equal) || key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)) step_ui_scale(+1);
	else if (key(ImGuiMod_Ctrl | ImGuiKey_Minus) || key(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract)) step_ui_scale(-1);
	else if (key(ImGuiMod_Ctrl | ImGuiKey_0) || key(ImGuiMod_Ctrl | ImGuiKey_Keypad0)) set_ui_scale(0);
	else return false;
	return true;
}

// Rebuilds the font at the new size (sharp, not stretched) and scales the style. Between frames only.
static void apply_ui_scale(float scale)
{
	ImGuiStyle &style = ImGui::GetStyle();
	style = ImGuiStyle();
	ImGui::StyleColorsDark(&style);
	style.FrameRounding = 4.0f;
	style.ScaleAllSizes(scale);

	ImGuiIO &io = ImGui::GetIO();
	io.Fonts->Clear();
	ImFontConfig font;
	font.SizePixels = std::round(13 * scale);
	io.FontDefault = io.Fonts->AddFontDefault(&font);
	ImGui_ImplSDLRenderer2_DestroyFontsTexture(); // made again by the next NewFrame
	g_applied = scale;
}

// Drawn with SDL_Renderer: Direct3D on Windows, OpenGL / Vulkan / software on Linux,
// so it also works on machines without a usable OpenGL driver (e.g. Windows on ARM).
bool run_gui(const char *title, int width, int height, const GuiCallbacks &cb)
{
	SDL_SetMainReady();
	SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return false;
	}
	if (g_scale <= 0) g_scale = screen_ui_scale();
	SDL_Rect usable;
	if (SDL_GetDisplayUsableBounds(0, &usable) == 0) { // bigger for a bigger UI, but not off the screen
		width = std::min((int)(width * g_scale), usable.w);
		height = std::min((int)(height * g_scale), usable.h);
	}
	SDL_Window *win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
	                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}
	g_window = win;
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); // no GPU driver: still usable
	if (!ren) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		SDL_DestroyWindow(win);
		g_window = nullptr;
		SDL_Quit();
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
	ImGui_ImplSDLRenderer2_Init(ren);

	std::string shown_title = title;
	bool quit = false;
	while (!quit) {
		SDL_Event ev;
		int timeout = cb.busy && cb.busy() ? 30 : 100; // wake up to read output / advance sequences
		if (SDL_WaitEventTimeout(&ev, timeout)) {
			bool close = false;
			do {
				ImGui_ImplSDL2_ProcessEvent(&ev);
				if (ev.type == SDL_QUIT) close = true;
				if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) close = true;
			} while (SDL_PollEvent(&ev));
			if (close) quit = !cb.close_requested || cb.close_requested();
		}
		if (cb.update) cb.update();
		if (cb.should_quit && cb.should_quit()) quit = true;
		if (cb.title) {
			std::string t = cb.title();
			if (t != shown_title) SDL_SetWindowTitle(win, (shown_title = t).c_str());
		}

		if (g_scale <= 0) g_scale = screen_ui_scale();
		if (g_scale != g_applied) {
			apply_ui_scale(g_scale);
			if (cb.scale_changed) cb.scale_changed(g_scale);
		}

		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();
		if (cb.draw) cb.draw();
		ImGui::Render();
		SDL_RenderSetScale(ren, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
		SDL_SetRenderDrawColor(ren, 20, 20, 26, 255);
		SDL_RenderClear(ren);
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
		SDL_RenderPresent(ren);
	}

	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	g_window = nullptr;
	g_applied = 0;
	SDL_Quit();
	return true;
}

bool text_field(const char *id, std::string &s, float width, bool multiline)
{
	ImGui::SetNextItemWidth(width);
	if (multiline) return ImGui::InputTextMultiline(id, &s, ImVec2(width, ImGui::GetTextLineHeight() * 3.4f));
	return ImGui::InputText(id, &s);
}

void status_dot(bool on)
{
	ImVec4 c = on ? ImVec4(0.2f, 0.85f, 0.3f, 1) : ImVec4(0.45f, 0.45f, 0.45f, 1);
	ImVec2 p = ImGui::GetCursorScreenPos();
	float r = ImGui::GetTextLineHeight() * 0.35f;
	ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r + 2, p.y + ImGui::GetTextLineHeight() * 0.5f), r,
	                                            ImGui::ColorConvertFloat4ToU32(c));
	ImGui::Dummy(ImVec2(r * 2 + 6, ImGui::GetTextLineHeight()));
}

void row_label(const char *text)
{
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(text);
	ImGui::SameLine(label_width());
}
