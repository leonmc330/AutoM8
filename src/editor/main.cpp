// autom8-editor: edits the buttons and block sequences of a sequence file.
//
//   autom8-editor                         edits the default sequence file (see autom8 --help)
//   autom8-editor [--sequence] FILE.json   edits another file

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
	Editor ed(document_path(file));
	GuiCallbacks cb;
	cb.draw = [&] { ed.draw(); };
	return run_gui("AutoM8 Editor " AUTOM8_VERSION, 1150, 740, cb) ? 0 : 1;
}

#ifdef _WIN32
// The editor is a GUI-subsystem program on Windows (no console window): its entry point is WinMain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
