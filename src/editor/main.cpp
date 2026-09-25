// autom8-editor: edits the buttons and block sequences of a sequence file.
//
//   autom8-editor              edits the default sequence file (see autom8 --help)
//   autom8-editor FILE.json    edits another file

#include "editor.hpp"
#include "gui.hpp"
#include "util.hpp"

int main(int argc, char **argv)
{
	Editor ed(document_path(utf8_args(argc, argv)));
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
