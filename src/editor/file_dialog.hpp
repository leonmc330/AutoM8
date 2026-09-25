// Native "Open" / "Save as" file dialogs, run on a thread so the window keeps drawing.
// Linux: kdialog or zenity (whichever is installed, the desktop's own first). Windows: the common dialog.
#pragma once

#include <future>
#include <string>

struct FileDialogResult {
	std::string path;  // "" = cancelled (or failed: see error)
	std::string error; // no dialog program, ...
};

class FileDialog {
public:
	enum Kind { Open, Save };

	// `start` is the file or folder the dialog opens in. False if a dialog is already open.
	bool start(Kind kind, const std::string &start);
	bool open() const { return pending_.valid(); }
	// True once, when the dialog is closed; `out` gets what the user chose.
	bool poll(FileDialogResult &out);

private:
	std::future<FileDialogResult> pending_;
};
