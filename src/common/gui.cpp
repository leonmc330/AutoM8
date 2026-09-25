#include "gui.hpp"

#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "misc/cpp/imgui_stdlib.h"

#define SDL_MAIN_HANDLED // we have our own main()
#include <SDL.h>
#include <cstdio>

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
	SDL_Window *win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
	                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); // no GPU driver: still usable
	if (!ren) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		SDL_DestroyWindow(win);
		SDL_Quit();
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	ImGui::StyleColorsDark();
	ImGui::GetStyle().FrameRounding = 4.0f;
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
	ImGui::SameLine(kLabelWidth);
}
