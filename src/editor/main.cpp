// autom8-editor: edits the buttons and block sequences of a sequence file.
//
//   autom8-editor                         edits the file opened last time, else the default one (see autom8 --help)
//   autom8-editor [--sequence] FILE.json   edits another file

#include "config.hpp"
#include "editor.hpp"
#include "gui.hpp"
#include "util.hpp"

int main(int argc, char **argv)
{
	std::vector<std::string> args = utf8_args(argc, argv);
	std::string file;
	for (size_t i = 1; i < args.size(); i++) {
		if (args[i] == "--sequence" && i + 1 < args.size()) file = args[++i];
		else if (!args[i].empty() && args[i][0] != '-' && file.empty()) file = args[i];
	}
	Config cfg = load_config();
	std::error_code ec;
	if (file.empty() && !cfg.last_file.empty() && std::filesystem::exists(path_of(cfg.last_file), ec))
		file = cfg.last_file;
	set_ui_scale(cfg.ui_scale); // 0 (first run): fit the screen, then saved

	Editor ed(document_path(file));
	GuiCallbacks cb;
	cb.scale_changed = [&](float s) {
		if (s != cfg.ui_scale && save_ui_scale(s)) cfg.ui_scale = s;
	};
	cb.draw = [&] { ed.draw(); };
	cb.busy = [&] { return ed.dialog.open(); }; // notice soon when the file dialog closes
	cb.title = [&] { return ed.title(); };
	cb.close_requested = [&] { return ed.close_requested(); };
	cb.should_quit = [&] { return ed.quit; };
	return run_gui(ed.title().c_str(), 1150, 740, cb) ? 0 : 1;
}

#ifdef _WIN32
// The editor is a GUI-subsystem program on Windows (no console window): its entry point is WinMain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
