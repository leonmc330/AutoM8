#include "file_dialog.hpp"
#include "util.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>

static FileDialogResult run_dialog(FileDialog::Kind kind, std::string start)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); // the dialog uses COM
	wchar_t file[4096] = L"";
	std::wstring dir;
	std::error_code ec;
	if (!start.empty() && fs::is_directory(path_of(start), ec)) {
		dir = widen(start);
	} else if (!start.empty()) {
		std::wstring name = path_of(start).filename().wstring();
		dir = path_of(start).parent_path().wstring();
		wcsncpy(file, name.c_str(), 4095);
	}
	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof ofn;
	ofn.lpstrFilter = L"Sequence files (*.json)\0*.json\0All files (*.*)\0*.*\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = 4096;
	ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
	ofn.lpstrDefExt = L"json";
	ofn.Flags = OFN_NOCHANGEDIR | OFN_EXPLORER;
	FileDialogResult r;
	BOOL ok;
	if (kind == FileDialog::Open) {
		ofn.Flags |= OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		ok = GetOpenFileNameW(&ofn);
	} else {
		ofn.Flags |= OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
		ok = GetSaveFileNameW(&ofn);
	}
	if (ok) r.path = narrow(file);
	else if (DWORD e = CommDlgExtendedError()) r.error = "The file dialog failed (error " + std::to_string(e) + ")";
	CoUninitialize();
	return r;
}
#else
#include <sys/wait.h>

static std::string quote(const std::string &s)
{
	std::string q = "'";
	for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
	return q + "'";
}

static bool installed(const char *program)
{
	return system(("command -v " + std::string(program) + " >/dev/null 2>&1").c_str()) == 0;
}

static FileDialogResult run_dialog(FileDialog::Kind kind, std::string start)
{
	FileDialogResult r;
	std::string desktop;
	get_env("XDG_CURRENT_DESKTOP", desktop);
	bool kde = desktop.find("KDE") != std::string::npos;
	bool has_kdialog = installed("kdialog"), has_zenity = installed("zenity");
	std::string cmd;
	if (has_kdialog && (kde || !has_zenity)) {
		cmd = std::string("kdialog --title ") + (kind == FileDialog::Open ? "'Open'" : "'Save as'") +
		      (kind == FileDialog::Open ? " --getopenfilename " : " --getsavefilename ") + quote(start) +
		      " 'application/json all/allfiles'";
	} else if (has_zenity) {
		cmd = "zenity --file-selection --title=" + std::string(kind == FileDialog::Open ? "'Open'" : "'Save as'") +
		      (kind == FileDialog::Save ? " --save" : "") + " --filename=" + quote(start) +
		      " --file-filter='Sequence files | *.json' --file-filter='All files | *'";
	} else {
		r.error = "No file dialog: install zenity or kdialog";
		return r;
	}
	FILE *p = popen((cmd + " 2>/dev/null").c_str(), "r");
	if (!p) {
		r.error = "Cannot start the file dialog";
		return r;
	}
	char buf[4096];
	std::string out;
	while (size_t n = fread(buf, 1, sizeof buf, p)) out.append(buf, n);
	int status = pclose(p);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) r.path = trim(out); // 1 = cancelled
	return r;
}
#endif

bool FileDialog::start(Kind kind, const std::string &start)
{
	if (open()) return false;
	pending_ = std::async(std::launch::async, run_dialog, kind, start);
	return true;
}

bool FileDialog::poll(FileDialogResult &out)
{
	if (!open() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
	out = pending_.get(); // leaves pending_ invalid: no dialog open
	return true;
}
