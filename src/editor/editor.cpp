// Editor window: button list on the left, the selected button's blocks on the
// right (inline fields, add / delete / move by buttons or drag & drop).
// A File menu saves / opens / reloads, and asks before dropping unsaved changes;
// an Interface menu sets the UI scale.
#include "editor.hpp"
#include "config.hpp"
#include "gui.hpp"
#include "moves.hpp"
#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

// ------------------------------------------------------- moving blocks ---

// Moves are recorded while drawing and applied once the frame is drawn.
static Move g_move;
static bool g_move_pending = false;

static void request_move(const SeqPath &from, int from_idx, const SeqPath &to, int to_idx)
{
	g_move = {from, from_idx, false, to, to_idx};
	g_move_pending = true;
}

static void request_delete(const SeqPath &from, int from_idx)
{
	g_move = {from, from_idx, true, {}, -1};
	g_move_pending = true;
}

struct DragPayload {
	SeqPath seq;
	int idx;
};

static void drop_target(const SeqPath &seq, int at)
{
	if (!ImGui::BeginDragDropTarget()) return;
	if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("BLOCK")) {
		const DragPayload *d = (const DragPayload *)pl->Data;
		request_move(d->seq, d->idx, seq, at);
	}
	ImGui::EndDragDropTarget();
}

// -------------------------------------------------------------- fields ---

static void edit_condition(Condition &c, bool &dirty, float w)
{
	row_label("Condition");
	dirty |= ImGui::Checkbox("not", &c.negate);
	ImGui::SameLine();
	int t = (int)c.type;
	ImGui::SetNextItemWidth(std::max(px(120), w - label_width() - px(60)));
	if (ImGui::Combo("##ct", &t, kCondLabels, kCondCount)) {
		c.type = (CT)t;
		dirty = true;
	}
	float fw = w - label_width();
	switch (c.type) {
	case CT::OutputMatches:
		row_label("Program"); dirty |= text_field("##cp", c.program, fw);
		row_label("Regex");   dirty |= text_field("##ctx", c.text, fw);
		break;
	case CT::ProgramRunning: row_label("Program"); dirty |= text_field("##cp", c.program, fw); break;
	case CT::ProcessRunning: row_label("Patterns"); dirty |= text_field("##ctx", c.text, fw); break;
	case CT::FileExists: row_label("File"); dirty |= text_field("##ctx", c.text, fw); break;
	case CT::CommandSucceeds: row_label("Command"); dirty |= text_field("##ctx", c.text, fw); break;
	case CT::ExitCodeIs:
		row_label("Program");   dirty |= text_field("##cp", c.program, fw);
		row_label("Exit code"); ImGui::SetNextItemWidth(px(100)); dirty |= ImGui::InputInt("##cn", &c.number);
		break;
	case CT::UserSaysYes: row_label("Question"); dirty |= text_field("##ctx", c.text, fw); break;
	}
}

static void edit_run_fields(Block &b, bool &dirty, float w)
{
	float fw = w - label_width();
	row_label("Name");    dirty |= text_field("##n", b.name, fw);
	row_label("Command"); dirty |= text_field("##c", b.command, fw, b.shell);
	ImGui::SetCursorPosX(label_width());
#ifdef _WIN32
	dirty |= ImGui::Checkbox("shell (cmd /c)", &b.shell);
#else
	dirty |= ImGui::Checkbox("shell (sh -c)", &b.shell);
#endif
	ImGui::SameLine();
	dirty |= ImGui::Checkbox("skip if already running", &b.skip_if_running);
	ImGui::SetCursorPosX(label_width());
	dirty |= ImGui::Checkbox("blocking", &b.blocking);
	if (b.blocking) {
		ImGui::SameLine(); ImGui::TextUnformatted("min s"); ImGui::SameLine();
		ImGui::SetNextItemWidth(px(70)); dirty |= ImGui::InputFloat("##min", &b.min_s, 0, 0, "%.1f");
		ImGui::SameLine(); ImGui::TextUnformatted("max s (-1 = none)"); ImGui::SameLine();
		ImGui::SetNextItemWidth(px(70)); dirty |= ImGui::InputFloat("##max", &b.max_s, 0, 0, "%.1f");
	}
	if (ImGui::TreeNode("more")) {
		row_label("Working dir"); dirty |= text_field("##cwd", b.cwd, fw - px(20));
		row_label("Env K=V");     dirty |= text_field("##env", b.env, fw - px(20), true);
		row_label("Match");       dirty |= text_field("##m", b.match, fw - px(20));
		ImGui::TextDisabled("Match: comma separated parts of the process command line (detect / kill it)");
		ImGui::TreePop();
	}
}

// ------------------------------------------------------------- blocks ---

static void edit_seq(Seq &seq, const SeqPath &path, bool &dirty);

static void nested(const char *label, Seq &s, const SeqPath &parent, int idx, bool other, bool &dirty)
{
	ImGui::TextUnformatted(label);
	ImGui::Indent(px(16));
	ImGui::PushID(label);
	if (parent.can_descend()) edit_seq(s, parent.child(idx, other), dirty);
	else ImGui::TextDisabled("(nested too deep to edit here)");
	ImGui::PopID();
	ImGui::Unindent(16);
}

static void block_header(Seq &seq, const SeqPath &path, int i, const BlockInfo &bi)
{
	Block &b = seq[i];
	ImGui::PushStyleColor(ImGuiCol_Button, to_imvec(bi.color));
	ImGui::SmallButton("::"); // drag handle
	ImGui::PopStyleColor();
	if (ImGui::BeginDragDropSource()) {
		DragPayload d = {path, i};
		ImGui::SetDragDropPayload("BLOCK", &d, sizeof d);
		ImGui::TextUnformatted(bi.label);
		ImGui::EndDragDropSource();
	}
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", bi.label);
	if (b.type == BT::Run && !b.name.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("%s", b.name.c_str());
	}
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - px(76));
	if (ImGui::SmallButton("^") && i > 0) request_move(path, i, path, i - 1);
	ImGui::SameLine();
	if (ImGui::SmallButton("v") && i + 1 < (int)seq.size()) request_move(path, i, path, i + 2);
	ImGui::SameLine();
	if (ImGui::SmallButton("x")) request_delete(path, i);
}

static void edit_block(Seq &seq, const SeqPath &path, int i, bool &dirty)
{
	Block &b = seq[i];
	const BlockInfo &bi = info(b.type);
	ImGui::PushID(&b);

	ImVec4 bg = to_imvec(bi.color);
	bg.w = 0.28f;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
	ImGui::PushStyleColor(ImGuiCol_Border, to_imvec(bi.color));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(6));
	ImGui::BeginChild("blk", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

	block_header(seq, path, i, bi);

	float w = ImGui::GetContentRegionAvail().x;
	float fw = w - label_width();
	switch (b.type) {
	case BT::Run: edit_run_fields(b, dirty, w); break;
	case BT::WaitSeconds:
		row_label("Seconds"); ImGui::SetNextItemWidth(px(90)); dirty |= ImGui::InputFloat("##s", &b.seconds, 0, 0, "%.1f");
		break;
	case BT::WaitUntil:
		edit_condition(b.cond, dirty, w);
		row_label("Timeout s"); ImGui::SetNextItemWidth(px(90)); dirty |= ImGui::InputFloat("##s", &b.seconds, 0, 0, "%.1f");
		ImGui::SameLine(); ImGui::TextDisabled("-1 = forever"); ImGui::SameLine();
		dirty |= ImGui::Checkbox("stop sequence on timeout", &b.stop_on_timeout);
		break;
	case BT::If:
		edit_condition(b.cond, dirty, w);
		nested("then", b.body, path, i, false, dirty);
		nested("else", b.else_body, path, i, true, dirty);
		break;
	case BT::RepeatN:
		row_label("Times"); ImGui::SetNextItemWidth(px(90)); dirty |= ImGui::InputInt("##n", &b.count);
		ImGui::SameLine(); ImGui::TextDisabled("-1 = forever");
		nested("do", b.body, path, i, false, dirty);
		break;
	case BT::RepeatUntil:
		edit_condition(b.cond, dirty, w);
		nested("do", b.body, path, i, false, dirty);
		break;
	case BT::KillProgram: row_label("Program"); dirty |= text_field("##n", b.name, fw); break;
	case BT::KillMatching:
		row_label("Patterns"); dirty |= text_field("##m", b.match, fw);
		ImGui::TextDisabled("Every process whose command line contains one of these (comma separated)");
		break;
	case BT::KillAll: ImGui::TextDisabled("Every program from the Run blocks, however it was started"); break;
	case BT::Message:
		row_label("Text"); dirty |= text_field("##t", b.command, fw);
		ImGui::SetCursorPosX(label_width());
		dirty |= ImGui::Checkbox("show as popup", &b.popup);
		break;
	case BT::Stop: ImGui::TextDisabled("Ends the sequence here"); break;
	case BT::Throw:
		row_label("Error text"); dirty |= text_field("##t", b.command, fw);
		ImGui::TextDisabled("Stops the sequence and shows this as an error");
		break;
	}
	if (b.type == BT::KillProgram || b.type == BT::KillMatching || b.type == BT::KillAll) {
		row_label("Graceful s");
		ImGui::SetNextItemWidth(px(90));
		dirty |= ImGui::InputFloat("##g", &b.graceful_s, 0, 0, "%.1f");
		ImGui::SameLine();
		ImGui::TextDisabled("0 = hard kill now, else ask to quit first (SIGTERM / close its windows), hard kill after this long");
	}

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(2);
	drop_target(path, i); // dropping onto a block inserts before it
	ImGui::PopID();
}

static void add_menu(Seq &seq, bool &dirty)
{
	if (!ImGui::BeginPopup("add")) return;
	for (int i = 0; i < kBlockCount; i++) {
		const BlockInfo &bi = kBlocks[i];
		ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(bi.color));
		ImGui::Bullet();
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (ImGui::Selectable(bi.label)) {
			Block b;
			b.type = bi.type;
			if (b.type == BT::WaitUntil) b.seconds = -1;
			seq.push_back(b);
			dirty = true;
		}
	}
	ImGui::EndPopup();
}

static void edit_seq(Seq &seq, const SeqPath &path, bool &dirty)
{
	for (int i = 0; i < (int)seq.size(); i++) edit_block(seq, path, i, dirty);
	if (ImGui::SmallButton("+ add block")) ImGui::OpenPopup("add");
	drop_target(path, (int)seq.size()); // dropping onto "+" appends
	add_menu(seq, dirty);
}

// ------------------------------------------------------------- buttons ---

static std::string unique_name(const Document &doc, const std::string &base)
{
	for (int n = 1;; n++) {
		std::string name = n == 1 ? base : base + " " + std::to_string(n);
		bool used = false;
		for (auto &b : doc.buttons) used |= b.name == name;
		if (!used) return name;
	}
}

// Left panel: the list of buttons + properties of the selected one.
static void button_panel(Editor &ed, bool &dirty)
{
	Document &doc = ed.doc;
	ImGui::TextUnformatted("Buttons");
	ImGui::Separator();
	for (int i = 0; i < (int)doc.buttons.size(); i++) {
		Button &b = doc.buttons[i];
		ImGui::PushID(i);
		ImGui::ColorButton("##c", to_imvec(b.color), ImGuiColorEditFlags_NoTooltip, ImVec2(px(14), px(14)));
		ImGui::SameLine();
		if (ImGui::Selectable(b.name.c_str(), ed.selected == i)) ed.selected = i;
		ImGui::PopID();
	}
	if (ImGui::Button("+ add button")) {
		Button b;
		b.name = unique_name(doc, "Button");
		doc.buttons.push_back(b);
		ed.selected = (int)doc.buttons.size() - 1;
		dirty = true;
	}

	if (ed.selected < 0 || ed.selected >= (int)doc.buttons.size()) return;
	Button &b = doc.buttons[ed.selected];
	ImGui::Spacing();
	ImGui::SeparatorText("Selected button");
	ImGui::TextUnformatted("Name");
	dirty |= text_field("##bn", b.name, -1);
	if (std::count_if(doc.buttons.begin(), doc.buttons.end(), [&](const Button &o) { return o.name == b.name; }) > 1)
		ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Another button has this name");
	ImGui::TextUnformatted("Color");
	float col[3] = {b.color.r, b.color.g, b.color.b};
	if (ImGui::ColorEdit3("##col", col, ImGuiColorEditFlags_NoInputs)) {
		b.color = {col[0], col[1], col[2], 1};
		dirty = true;
	}
	int i = ed.selected;
	if (ImGui::Button("^") && i > 0) {
		std::swap(doc.buttons[i], doc.buttons[i - 1]);
		ed.selected--;
		dirty = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("v") && i + 1 < (int)doc.buttons.size()) {
		std::swap(doc.buttons[i], doc.buttons[i + 1]);
		ed.selected++;
		dirty = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Duplicate")) {
		Button copy = doc.buttons[i];
		copy.name = unique_name(doc, copy.name);
		doc.buttons.insert(doc.buttons.begin() + i + 1, copy);
		ed.selected = i + 1;
		dirty = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete")) ImGui::OpenPopup("delete?");
	if (ImGui::BeginPopupModal("delete?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Delete the button '%s' and its blocks?", doc.buttons[i].name.c_str());
		if (ImGui::Button("Delete")) {
			doc.buttons.erase(doc.buttons.begin() + i);
			ed.selected = std::min(ed.selected, (int)doc.buttons.size() - 1);
			dirty = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

// ------------------------------------------------------------- window ---

Editor::Editor(const std::string &p) : path(p)
{
	std::string err;
	bool incompatible = false;
	if (load_document(path, doc, &err, &incompatible)) {
		status = "Opened " + path;
		remember_file();
	} else if (incompatible) { // saving must not replace it: start an untitled file instead
		status = err;
		path.clear();
		doc = Document();
		doc.buttons.push_back(Button());
	} else {
		std::error_code ec;
		status = fs::exists(path_of(path), ec) ? "Cannot read the file (" + err + "): saving will replace it"
		                                       : "New file, Ctrl+S creates it";
		doc = default_document();
	}
	saved_json = document_json(doc);
}

std::string Editor::title() const
{
	std::string name = path.empty() ? "Untitled" : path_string(path_of(path).filename());
	return name + (modified ? "*" : "") + " - AutoM8 Editor " AUTOM8_VERSION;
}

bool Editor::load(const std::string &file)
{
	Document d;
	std::string err;
	if (!load_document(file, d, &err)) {
		status = err;
		return false;
	}
	doc = std::move(d);
	path = file;
	saved_json = document_json(doc);
	modified = false;
	selected = 0;
	status = "Opened " + path;
	remember_file();
	return true;
}

void Editor::remember_file()
{
	if (!path.empty()) save_last_file(path);
}

bool Editor::write(const std::string &file)
{
	if (!save_document(file, doc)) {
		status = "Cannot save " + file;
		return false;
	}
	path = file;
	saved_json = document_json(doc);
	modified = false;
	status = "Saved " + path;
	remember_file();
	return true;
}

bool Editor::save()
{
	if (path.empty()) {
		save_as();
		return false; // saved later, when a file is chosen
	}
	return write(path);
}

void Editor::save_as()
{
	std::string start = path.empty() ? path_string(path_of(config_dir()) / "sequences.json") : path;
	dialog_kind = FileDialog::Save;
	if (!dialog.start(FileDialog::Save, start)) after_save = None;
}

void Editor::request(Action a)
{
	if (dialog.open()) return; // one thing at a time
	if (a == Reload && path.empty()) {
		status = "Nothing to reload: this file was never saved";
		return;
	}
	if (!modified) return run(a);
	pending = a;
	ask_pending = true;
}

void Editor::run(Action a)
{
	switch (a) {
	case New:
		doc = Document();
		doc.buttons.push_back(Button());
		path.clear();
		saved_json = document_json(doc);
		modified = false;
		selected = 0;
		status = "New file";
		break;
	case Open:
		dialog_kind = FileDialog::Open;
		dialog.start(FileDialog::Open, path.empty() ? config_dir() : path);
		break;
	case Reload:
		if (load(path)) status = "Reloaded " + path;
		break;
	case Quit: quit = true; break;
	case None: break;
	}
}

bool Editor::close_requested()
{
	if (pending == Quit || dialog.open()) return false; // already asking / a file dialog is open
	request(Quit);
	return quit;
}

void Editor::poll_dialog()
{
	FileDialogResult r;
	if (!dialog.poll(r)) return;
	Action then = after_save;
	after_save = None;
	if (!r.error.empty()) {
		status = r.error;
	} else if (r.path.empty()) {
		status = "Cancelled";
	} else if (dialog_kind == FileDialog::Open) {
		load(r.path);
	} else if (write(with_json_extension(r.path))) {
		run(then); // "Save" before New / Open / Quit on a file that was never saved
	}
}

void Editor::menu_bar()
{
	bool ask_reset = false;
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New", "Ctrl+N")) request(New);
			if (ImGui::MenuItem("Open...", "Ctrl+O")) request(Open);
			if (ImGui::MenuItem("Reload from disk", "Ctrl+R", false, !path.empty())) request(Reload);
			ImGui::Separator();
			if (ImGui::MenuItem("Save", "Ctrl+S")) save();
			if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) save_as();
			ImGui::Separator();
			if (ImGui::MenuItem("Reset to default buttons...")) ask_reset = true;
			ImGui::Separator();
			if (ImGui::MenuItem("Quit", "Ctrl+Q")) request(Quit);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Interface")) {
			ImGui::SeparatorText("UI scale");
			for (int i = 0; i < kUiScaleCount; i++) {
				char label[16];
				snprintf(label, sizeof label, "%gx", kUiScales[i]);
				if (ImGui::MenuItem(label, nullptr, std::abs(ui_scale() - kUiScales[i]) < 0.01f)) set_ui_scale(kUiScales[i]);
			}
			ImGui::Separator();
			char fit[48];
			snprintf(fit, sizeof fit, "Fit the screen (%gx)", screen_ui_scale());
			if (ImGui::MenuItem(fit, "Ctrl+0")) set_ui_scale(0);
			if (ImGui::MenuItem("Bigger", "Ctrl+=")) step_ui_scale(+1);
			if (ImGui::MenuItem("Smaller", "Ctrl+-")) step_ui_scale(-1);
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
	if (ask_reset) ImGui::OpenPopup("Reset?");
}

void Editor::shortcuts()
{
	if (dialog.open() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
	auto key = [](ImGuiKeyChord chord) { return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal); };
	if (key(ImGuiMod_Ctrl | ImGuiKey_N)) request(New);
	if (key(ImGuiMod_Ctrl | ImGuiKey_O)) request(Open);
	if (key(ImGuiMod_Ctrl | ImGuiKey_R) || key(ImGuiKey_F5)) request(Reload);
	if (key(ImGuiMod_Ctrl | ImGuiKey_S)) save();
	if (key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) save_as();
	if (key(ImGuiMod_Ctrl | ImGuiKey_Q)) request(Quit);
	ui_scale_shortcuts();
}

void Editor::popups()
{
	std::string name = path.empty() ? "Untitled" : path_string(path_of(path).filename());
	if (ask_pending) {
		ImGui::OpenPopup(pending == Reload ? "Reload?" : "Unsaved changes");
		ask_pending = false;
	}
	auto center = [] { ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); };

	center();
	if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		const char *what = pending == New ? "starting a new file" : pending == Open ? "opening another file" : "closing";
		ImGui::Text("Save the changes to '%s' before %s?", name.c_str(), what);
		ImGui::Spacing();
		if (ImGui::Button("Save", ImVec2(px(110), 0))) {
			if (path.empty()) {
				after_save = pending;
				save_as();
			} else if (write(path)) {
				run(pending);
			}
			pending = None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't save", ImVec2(px(110), 0))) {
			run(pending);
			pending = None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			pending = None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	center();
	if (ImGui::BeginPopupModal("Reload?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Discard your unsaved changes and reload '%s' from disk?", name.c_str());
		ImGui::Spacing();
		if (ImGui::Button("Reload", ImVec2(px(110), 0))) {
			run(Reload);
			pending = None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			pending = None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	center();
	if (ImGui::BeginPopupModal("Reset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted("Replace every button with the default ones?");
		ImGui::Spacing();
		if (ImGui::Button("Reset", ImVec2(px(110), 0))) {
			doc = default_document();
			selected = 0;
			changed();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Keeps the document from changing under a file dialog (it's a separate window, maybe behind this one).
	if (dialog.open() && !ImGui::IsPopupOpen("File dialog")) ImGui::OpenPopup("File dialog");
	center();
	if (ImGui::BeginPopupModal("File dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
		ImGui::TextUnformatted(dialog_kind == FileDialog::Open ? "Choose a file to open..." : "Choose where to save...");
		if (!dialog.open()) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void Editor::draw()
{
	poll_dialog();
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::Begin("editor", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
	menu_bar();
	shortcuts();

	bool dirty = false;
	float status_h = ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("main", ImVec2(0, -status_h));
	// A new table per UI scale: a table keeps its column widths, this gives the left one its new default.
	std::string layout = "layout" + std::to_string((int)(ui_scale() * 100));
	if (ImGui::BeginTable(layout.c_str(), 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthFixed, px(250));
		ImGui::TableSetupColumn("blocks", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextColumn();
		ImGui::BeginChild("left");
		button_panel(*this, dirty);
		ImGui::EndChild();

		ImGui::TableNextColumn();
		ImGui::BeginChild("right");
		if (selected >= 0 && selected < (int)doc.buttons.size()) {
			Button &b = doc.buttons[selected];
			ImGui::PushStyleColor(ImGuiCol_ChildBg, to_imvec(b.color));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(10));
			ImGui::BeginChild("hat", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
			ImGui::TextColored(ImVec4(1, 1, 1, 1), "When \"%s\" is pressed", b.name.c_str());
			ImGui::EndChild();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
			ImGui::PushID(selected);
			SeqPath root;
			root.button = selected;
			edit_seq(b.seq, root, dirty);
			ImGui::PopID();
		} else {
			ImGui::TextDisabled("Add a button on the left.");
		}
		ImGui::EndChild();
		ImGui::EndTable();
	}
	ImGui::EndChild();
	if (g_move_pending) {
		g_move_pending = false;
		dirty |= apply_move(doc, g_move);
	}
	if (dirty) changed();

	// Status bar: the file, whether it is saved, the last thing that happened.
	ImGui::Separator();
	ImGui::TextUnformatted(path.empty() ? "Untitled" : path.c_str());
	ImGui::SameLine();
	if (modified) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "(modified, Ctrl+S to save)");
	else ImGui::TextDisabled("(saved)");
	ImGui::SameLine();
	ImGui::TextDisabled(" %s", status.c_str());

	popups();
	ImGui::End();
}
