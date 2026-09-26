// Runner window: a button per sequence, status line, popups, program list + logs.
#include "window.hpp"
#include "gui.hpp"
#include "runner.hpp"

#include <set>

static ImVec4 brighter(ImVec4 c) { return ImVec4(c.x * 1.2f, c.y * 1.2f, c.z * 1.2f, c.w); }

static void draw_buttons(Runner &r)
{
	for (size_t i = 0; i < r.doc.buttons.size(); i++) {
		const Button &b = r.doc.buttons[i];
		bool running = r.active && r.running_button == b.name;
		ImGui::PushID((int)i);
		ImVec4 c = to_imvec(b.color);
		ImGui::PushStyleColor(ImGuiCol_Button, c);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighter(c));
		ImGui::BeginDisabled(running);
		std::string label = running ? b.name + "..." : b.name;
		if (ImGui::Button(label.c_str(), ImVec2(px(150), px(48)))) r.press(b.name);
		ImGui::EndDisabled();
		ImGui::PopStyleColor(2);
		ImGui::PopID();
		// wrap to the next line when the row is full
		float next_x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + px(150);
		if (i + 1 < r.doc.buttons.size() && next_x < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
			ImGui::SameLine();
	}
	if (r.doc.buttons.empty()) ImGui::TextDisabled("No buttons in this file - add some with autom8-editor.");
}

static void draw_popups(Runner &r)
{
	if (r.ask_open) {
		ImGui::OpenPopup("Question");
		r.ask_open = false;
	}
	if (ImGui::BeginPopupModal("Question", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (!r.asking) ImGui::CloseCurrentPopup(); // the sequence moved on (cancelled, timed out)
		ImGui::TextUnformatted(r.ask_text.c_str());
		if (ImGui::Button("Yes", ImVec2(px(110), 0))) { r.ask_answer = 1; ImGui::CloseCurrentPopup(); }
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(px(110), 0))) { r.ask_answer = 0; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}

	if (!r.popup_text.empty()) ImGui::OpenPopup("Message");
	if (ImGui::BeginPopupModal("Message", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(r.popup_text.c_str());
		if (ImGui::Button("OK", ImVec2(px(110), 0))) { r.popup_text.clear(); ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}

	if (!r.error_text.empty()) ImGui::OpenPopup("Error");
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.65f, 0.10f, 0.12f, 1));
	if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", r.error_text.c_str());
		if (ImGui::Button("OK", ImVec2(px(110), 0))) { r.error_text.clear(); ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	ImGui::PopStyleColor();
}

static void draw_log(Proc &p)
{
	ImGui::PushID(&p);
	ImGui::SeparatorText((p.name + " log").c_str());
	if (ImGui::SmallButton("Clear")) p.clear_log();
	ImGui::SameLine();
	if (ImGui::SmallButton("Copy")) {
		std::string all;
		for (auto &l : p.log) all += l + "\n";
		ImGui::SetClipboardText(all.c_str());
	}
	ImGui::BeginChild("log", ImVec2(0, px(200)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5;
	ImGuiListClipper clip;
	clip.Begin((int)p.log.size());
	while (clip.Step())
		for (int l = clip.DisplayStart; l < clip.DisplayEnd; l++) ImGui::TextUnformatted(p.log[l].c_str());
	if (at_bottom) ImGui::SetScrollHereY(1.0f);
	ImGui::EndChild();
	ImGui::PopID();
}

static void draw_program_list(Runner &r)
{
	if (!ImGui::BeginTable("programs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
	ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(22));
	ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(130));
	auto row = [&](const std::string &name) {
		Proc &p = r.procs[name];
		p.name = name;
		ImGui::PushID(name.c_str());
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		status_dot(r.program_running(name));
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(name.c_str());
		if (!p.running() && p.exit_status >= 0) {
			ImGui::SameLine();
			ImGui::TextDisabled("(exit %d)", p.exit_status);
		}
		ImGui::TableNextColumn();
		ImGui::Checkbox("Log", &p.show_log);
		ImGui::SameLine();
		if (ImGui::SmallButton("Kill")) r.kill_program(name);
		ImGui::PopID();
	};
	std::set<std::string> shown;
	for (auto *b : r.programs()) {
		if (!shown.insert(b->name).second) continue;
		row(b->name);
		// the copies Thread blocks started: "name #1", "name #2"...
		std::vector<std::string> copies;
		std::string prefix = b->name + " #";
		for (auto it = r.procs.lower_bound(prefix); it != r.procs.end() && it->first.rfind(prefix, 0) == 0; ++it)
			copies.push_back(it->first);
		for (auto &c : copies) row(c);
	}
	ImGui::EndTable();
}

void draw_runner_window(Runner &r)
{
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::Begin("runner", nullptr,
	             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBringToFrontOnFocus);
	if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) ui_scale_shortcuts(); // Ctrl + / - / 0
	draw_buttons(r);
	ImGui::TextWrapped("%s", r.status.c_str());
	draw_popups(r);
	ImGui::Separator();
	draw_program_list(r);
	for (auto &kv : r.procs)
		if (kv.second.show_log) draw_log(kv.second);
	ImGui::End();
}
