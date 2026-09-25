// Editor window: button list on the left, the selected button's blocks on the
// right (inline fields, add / delete / move by buttons or drag & drop).
// Every change is saved to the file right away.
#include "editor.hpp"
#include "gui.hpp"
#include "moves.hpp"
#include "util.hpp"

#include <algorithm>
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
	ImGui::SetNextItemWidth(std::max(120.0f, w - kLabelWidth - 60));
	if (ImGui::Combo("##ct", &t, kCondLabels, kCondCount)) {
		c.type = (CT)t;
		dirty = true;
	}
	float fw = w - kLabelWidth;
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
		row_label("Exit code"); ImGui::SetNextItemWidth(100); dirty |= ImGui::InputInt("##cn", &c.number);
		break;
	case CT::UserSaysYes: row_label("Question"); dirty |= text_field("##ctx", c.text, fw); break;
	}
}

static void edit_run_fields(Block &b, bool &dirty, float w)
{
	float fw = w - kLabelWidth;
	row_label("Name");    dirty |= text_field("##n", b.name, fw);
	row_label("Command"); dirty |= text_field("##c", b.command, fw, b.shell);
	ImGui::SetCursorPosX(kLabelWidth);
#ifdef _WIN32
	dirty |= ImGui::Checkbox("shell (cmd /c)", &b.shell);
#else
	dirty |= ImGui::Checkbox("shell (sh -c)", &b.shell);
#endif
	ImGui::SameLine();
	dirty |= ImGui::Checkbox("skip if already running", &b.skip_if_running);
	ImGui::SetCursorPosX(kLabelWidth);
	dirty |= ImGui::Checkbox("blocking", &b.blocking);
	if (b.blocking) {
		ImGui::SameLine(); ImGui::TextUnformatted("min s"); ImGui::SameLine();
		ImGui::SetNextItemWidth(70); dirty |= ImGui::InputFloat("##min", &b.min_s, 0, 0, "%.1f");
		ImGui::SameLine(); ImGui::TextUnformatted("max s (-1 = none)"); ImGui::SameLine();
		ImGui::SetNextItemWidth(70); dirty |= ImGui::InputFloat("##max", &b.max_s, 0, 0, "%.1f");
	}
	if (ImGui::TreeNode("more")) {
		row_label("Working dir"); dirty |= text_field("##cwd", b.cwd, fw - 20);
		row_label("Env K=V");     dirty |= text_field("##env", b.env, fw - 20, true);
		row_label("Match");       dirty |= text_field("##m", b.match, fw - 20);
		ImGui::TextDisabled("Match: comma separated parts of the process command line (detect / kill it)");
		ImGui::TreePop();
	}
}

// ------------------------------------------------------------- blocks ---

static void edit_seq(Seq &seq, const SeqPath &path, bool &dirty);

static void nested(const char *label, Seq &s, const SeqPath &parent, int idx, bool other, bool &dirty)
{
	ImGui::TextUnformatted(label);
	ImGui::Indent(16);
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
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 76);
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
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
	ImGui::BeginChild("blk", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

	block_header(seq, path, i, bi);

	float w = ImGui::GetContentRegionAvail().x;
	float fw = w - kLabelWidth;
	switch (b.type) {
	case BT::Run: edit_run_fields(b, dirty, w); break;
	case BT::WaitSeconds:
		row_label("Seconds"); ImGui::SetNextItemWidth(90); dirty |= ImGui::InputFloat("##s", &b.seconds, 0, 0, "%.1f");
		break;
	case BT::WaitUntil:
		edit_condition(b.cond, dirty, w);
		row_label("Timeout s"); ImGui::SetNextItemWidth(90); dirty |= ImGui::InputFloat("##s", &b.seconds, 0, 0, "%.1f");
		ImGui::SameLine(); ImGui::TextDisabled("-1 = forever"); ImGui::SameLine();
		dirty |= ImGui::Checkbox("stop sequence on timeout", &b.stop_on_timeout);
		break;
	case BT::If:
		edit_condition(b.cond, dirty, w);
		nested("then", b.body, path, i, false, dirty);
		nested("else", b.else_body, path, i, true, dirty);
		break;
	case BT::RepeatN:
		row_label("Times"); ImGui::SetNextItemWidth(90); dirty |= ImGui::InputInt("##n", &b.count);
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
		ImGui::SetCursorPosX(kLabelWidth);
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
		ImGui::SetNextItemWidth(90);
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
		ImGui::ColorButton("##c", to_imvec(b.color), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
		ImGui::SameLine();
		std::string label = b.name + (doc.on_close == b.name ? "  (on close)" : "");
		if (ImGui::Selectable(label.c_str(), ed.selected == i)) ed.selected = i;
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
	std::string old_name = b.name;
	ImGui::TextUnformatted("Name");
	if (text_field("##bn", b.name, -1)) {
		if (doc.on_close == old_name) doc.on_close = b.name; // keep the on-close link
		dirty = true;
	}
	if (std::count_if(doc.buttons.begin(), doc.buttons.end(), [&](const Button &o) { return o.name == b.name; }) > 1)
		ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Another button has this name");
	ImGui::TextUnformatted("Color");
	float col[3] = {b.color.r, b.color.g, b.color.b};
	if (ImGui::ColorEdit3("##col", col, ImGuiColorEditFlags_NoInputs)) {
		b.color = {col[0], col[1], col[2], 1};
		dirty = true;
	}
	bool on_close = doc.on_close == b.name;
	if (ImGui::Checkbox("run when the runner closes", &on_close)) {
		doc.on_close = on_close ? b.name : "";
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
			if (doc.on_close == doc.buttons[i].name) doc.on_close.clear();
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

Editor::Editor(const std::string &p) : path(p), path_input(p)
{
	std::string err;
	if (!load_document(path, doc, &err)) {
		std::error_code ec;
		if (fs::exists(path_of(path), ec)) {
			// Every change is saved right away: keep the unreadable file first.
			std::string bak = path + ".bak";
			fs::copy_file(path_of(path), path_of(bak), fs::copy_options::overwrite_existing, ec);
			status = "Cannot read the file (" + err + "), starting from the default buttons. " +
			         (ec ? "Saving will overwrite it." : "The old file was copied to " + bak);
		} else {
			status = "New file: " + path;
		}
		doc = default_document();
	}
}

void Editor::save()
{
	if (save_document(path, doc)) status = "Saved " + path;
	else status = "Cannot save " + path;
}

static void file_bar(Editor &ed, bool &dirty)
{
	text_field("##file", ed.path_input, 460);
	ImGui::SameLine();
	if (ImGui::Button("Save as")) {
		ed.path = expand(ed.path_input);
		ed.save();
	}
	ImGui::SameLine();
	if (ImGui::Button("Open")) {
		Document d;
		std::string err;
		std::string file = expand(ed.path_input);
		if (load_document(file, d, &err)) {
			ed.doc = std::move(d);
			ed.path = file;
			ed.selected = 0;
			ed.status = "Opened " + ed.path;
		} else {
			ed.status = err;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset to default")) ImGui::OpenPopup("reset?");
	if (ImGui::BeginPopupModal("reset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted("Replace every button with the default ones?");
		if (ImGui::Button("Yes")) {
			ed.doc = default_document();
			ed.selected = 0;
			dirty = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", ed.status.c_str());
}

void Editor::draw()
{
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::Begin("editor", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBringToFrontOnFocus);
	bool dirty = false;
	file_bar(*this, dirty);
	ImGui::Separator();

	if (ImGui::BeginTable("layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("buttons", ImGuiTableColumnFlags_WidthFixed, 250);
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
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
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
	if (g_move_pending) {
		g_move_pending = false;
		dirty |= apply_move(doc, g_move);
	}
	if (dirty) save();
	ImGui::End();
}
