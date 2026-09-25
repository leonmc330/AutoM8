#pragma once

#include "blocks.hpp"
#include "file_dialog.hpp"

#include <string>

struct Editor {
	std::string path;       // the file being edited, "" = never saved (File > New)
	Document doc;
	std::string saved_json; // the document as it is on disk, to tell whether there are unsaved changes
	bool modified = false;
	int selected = 0;       // selected button
	std::string status;

	// Something that would drop unsaved changes, waiting for "Save / Don't save / Cancel".
	enum Action { None, New, Open, Reload, Quit };
	Action pending = None;
	bool ask_pending = false; // open the confirmation popup on the next frame
	Action after_save = None; // "Save" chose a file in the Save as dialog: do this once it is saved
	bool quit = false;        // the window can close

	FileDialog dialog;
	FileDialog::Kind dialog_kind = FileDialog::Open;

	explicit Editor(const std::string &path);
	void draw();
	std::string title() const;
	bool close_requested(); // the window's close button: true = close now

	bool load(const std::string &file); // false: status says why, the document is unchanged
	bool save();                        // Save as dialog first when there is no file yet
	void save_as();
	void request(Action a);             // asks first if there are unsaved changes
	void run(Action a);

private:
	bool write(const std::string &file);
	void changed() { modified = document_json(doc) != saved_json; }
	void remember_file(); // opened by default next time
	void poll_dialog();
	void menu_bar();
	void shortcuts();
	void popups();
};
