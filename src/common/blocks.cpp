#include "blocks.hpp"
#include "util.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

const BlockInfo kBlocks[] = {
	{BT::Run, "run", "Run", {0.26f, 0.45f, 0.85f, 1}},
	{BT::WaitSeconds, "wait", "Wait seconds", {0.85f, 0.62f, 0.15f, 1}},
	{BT::WaitUntil, "wait_until", "Wait until", {0.85f, 0.62f, 0.15f, 1}},
	{BT::If, "if", "If ... else", {0.85f, 0.45f, 0.15f, 1}},
	{BT::RepeatN, "repeat", "Repeat N times", {0.85f, 0.45f, 0.15f, 1}},
	{BT::RepeatUntil, "repeat_until", "Repeat until", {0.85f, 0.45f, 0.15f, 1}},
	{BT::KillProgram, "kill", "Kill program", {0.80f, 0.25f, 0.30f, 1}},
	{BT::KillMatching, "kill_matching", "Kill matching processes", {0.80f, 0.25f, 0.30f, 1}},
	{BT::KillAll, "kill_all", "Kill all programs", {0.80f, 0.25f, 0.30f, 1}},
	{BT::Message, "message", "Show message", {0.35f, 0.60f, 0.40f, 1}},
	{BT::Stop, "stop", "Stop sequence", {0.45f, 0.45f, 0.45f, 1}},
	{BT::Throw, "throw", "Throw error", {0.65f, 0.10f, 0.12f, 1}},
};
const int kBlockCount = sizeof(kBlocks) / sizeof(kBlocks[0]);

const BlockInfo &info(BT t)
{
	for (auto &b : kBlocks)
		if (b.type == t) return b;
	return kBlocks[0];
}

static const char *const kCondIds[] = {"output_matches",   "program_running", "process_running", "file_exists",
                                       "command_succeeds", "exit_code_is",    "user_says_yes"};
const char *const kCondLabels[] = {"output of program matches regex",
                                   "program is running",
                                   "process matching pattern is running",
                                   "file exists",
                                   "shell command succeeds",
                                   "last exit code of program is",
                                   "user answers Yes to"};
const int kCondCount = sizeof(kCondLabels) / sizeof(kCondLabels[0]);

Button *Document::find(const std::string &name)
{
	for (auto &b : buttons)
		if (b.name == name) return &b;
	return nullptr;
}

// ------------------------------------------------------------------ JSON ---

static json seq_to_json(const Seq &s);
static Seq seq_from_json(const json &a);

static json cond_to_json(const Condition &c)
{
	return {{"type", kCondIds[(int)c.type]}, {"not", c.negate}, {"program", c.program}, {"text", c.text},
	        {"number", c.number}};
}

static Condition cond_from_json(const json &j)
{
	Condition c;
	std::string t = j.value("type", kCondIds[1]);
	for (int i = 0; i < kCondCount; i++)
		if (t == kCondIds[i]) c.type = (CT)i;
	c.negate = j.value("not", false);
	c.program = j.value("program", "");
	c.text = j.value("text", "");
	c.number = j.value("number", 0);
	return c;
}

static json block_to_json(const Block &b)
{
	json j = {{"type", info(b.type).id}};
	switch (b.type) {
	case BT::Run:
		j.update({{"name", b.name}, {"command", b.command}, {"shell", b.shell}, {"cwd", b.cwd}, {"env", b.env},
		          {"match", b.match}, {"blocking", b.blocking}, {"min_seconds", b.min_s},
		          {"max_seconds", b.max_s}, {"skip_if_running", b.skip_if_running}});
		break;
	case BT::WaitSeconds: j["seconds"] = b.seconds; break;
	case BT::WaitUntil:
		j.update({{"condition", cond_to_json(b.cond)}, {"timeout", b.seconds}, {"stop_on_timeout", b.stop_on_timeout}});
		break;
	case BT::If:
		j.update({{"condition", cond_to_json(b.cond)}, {"then", seq_to_json(b.body)}, {"else", seq_to_json(b.else_body)}});
		break;
	case BT::RepeatN: j.update({{"count", b.count}, {"body", seq_to_json(b.body)}}); break;
	case BT::RepeatUntil: j.update({{"condition", cond_to_json(b.cond)}, {"body", seq_to_json(b.body)}}); break;
	case BT::KillProgram: j.update({{"name", b.name}, {"graceful_seconds", b.graceful_s}}); break;
	case BT::KillMatching: j.update({{"match", b.match}, {"graceful_seconds", b.graceful_s}}); break;
	case BT::KillAll: j["graceful_seconds"] = b.graceful_s; break;
	case BT::Message: j.update({{"text", b.command}, {"popup", b.popup}}); break;
	case BT::Throw: j["text"] = b.command; break;
	case BT::Stop: break;
	}
	return j;
}

static Block block_from_json(const json &j)
{
	Block b;
	std::string t = j.value("type", "message");
	for (auto &bi : kBlocks)
		if (t == bi.id) b.type = bi.type;
	b.name = j.value("name", "");
	b.command = j.value("command", j.value("text", ""));
	b.shell = j.value("shell", false);
	b.cwd = j.value("cwd", "");
	b.env = j.value("env", "");
	b.match = j.value("match", "");
	b.blocking = j.value("blocking", false);
	b.min_s = j.value("min_seconds", 0.0f);
	b.max_s = j.value("max_seconds", -1.0f);
	b.skip_if_running = j.value("skip_if_running", true);
	b.seconds = j.value("seconds", j.value("timeout", 1.0f));
	b.graceful_s = j.value("graceful_seconds", 0.0f);
	b.stop_on_timeout = j.value("stop_on_timeout", false);
	b.count = j.value("count", 3);
	b.popup = j.value("popup", false);
	if (j.contains("condition")) b.cond = cond_from_json(j["condition"]);
	if (j.contains("then")) b.body = seq_from_json(j["then"]);
	if (j.contains("body")) b.body = seq_from_json(j["body"]);
	if (j.contains("else")) b.else_body = seq_from_json(j["else"]);
	return b;
}

static json seq_to_json(const Seq &s)
{
	json a = json::array();
	for (auto &b : s) a.push_back(block_to_json(b));
	return a;
}

static Seq seq_from_json(const json &a)
{
	Seq s;
	if (a.is_array())
		for (auto &j : a) s.push_back(block_from_json(j));
	return s;
}

bool load_document(const std::string &path, Document &doc, std::string *error)
{
	std::ifstream f(path_of(path));
	if (!f) {
		if (error) *error = "cannot open " + path;
		return false;
	}
	try {
		json j = json::parse(f);
		Document d;
		if (j.contains("buttons")) {
			for (auto &jb : j["buttons"]) {
				Button b;
				b.name = jb.value("name", b.name);
				if (jb.contains("color") && jb["color"].is_array() && jb["color"].size() >= 3) {
					auto &c = jb["color"];
					b.color = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
					           c.size() > 3 ? c[3].get<float>() : 1.0f};
				}
				b.seq = seq_from_json(jb["sequence"]);
				d.buttons.push_back(b);
			}
			d.on_close = j.value("on_close", "");
		} else { // old format: {"start": [...], "stop": [...], "kill_on_close": bool}
			d.buttons.push_back({"Start", {0.20f, 0.55f, 0.25f, 1}, seq_from_json(j["start"])});
			d.buttons.push_back({"Stop", {0.60f, 0.20f, 0.22f, 1}, seq_from_json(j["stop"])});
			d.on_close = j.value("kill_on_close", true) ? "Stop" : "";
		}
		doc = d;
		return true;
	} catch (std::exception &e) {
		if (error) *error = path + ": " + e.what();
		return false;
	}
}

bool save_document(const std::string &path, const Document &doc)
{
	json buttons = json::array();
	for (auto &b : doc.buttons)
		buttons.push_back({{"name", b.name},
		                   {"color", {b.color.r, b.color.g, b.color.b, b.color.a}},
		                   {"sequence", seq_to_json(b.seq)}});
	json j = {{"buttons", buttons}, {"on_close", doc.on_close}};
	std::error_code ec;
	fs::path target = path_of(path);
	fs::create_directories(target.parent_path(), ec);
	fs::path tmp = path_of(path + ".tmp"); // write then rename, so the runner never reads half a file
	{
		std::ofstream f(tmp);
		f << j.dump(2) << "\n";
		f.close();
		if (!f) { // disk full, no permission...: keep the old file
			fs::remove(tmp, ec);
			return false;
		}
	}
	fs::rename(tmp, target, ec);
	if (ec) fs::remove(tmp, ec);
	return !ec;
}

// -------------------------------------------------------------- defaults ---

static Block message(const std::string &text, bool popup = false)
{
	Block b;
	b.type = BT::Message;
	b.command = text;
	b.popup = popup;
	return b;
}

Document default_document()
{
	Seq hello;
	{
		Block run;
		run.type = BT::Run;
		run.name = "Greeting";
		run.command = "echo Hello from AutoM8"; // through the shell: sh on Linux, cmd on Windows
		run.shell = true;
		run.blocking = true;
		run.max_s = 10;
		hello.push_back(run);

		Block check;
		check.type = BT::If;
		check.cond.type = CT::OutputMatches;
		check.cond.program = "Greeting";
		check.cond.text = "Hello";
		check.body.push_back(message("It works! Open autom8-editor to make your own buttons.", true));
		Block fail;
		fail.type = BT::Throw;
		fail.command = "The greeting printed nothing?";
		check.else_body.push_back(fail);
		hello.push_back(check);
	}

	Seq countdown;
	{
		Block wait;
		wait.type = BT::WaitSeconds;
		wait.seconds = 1;
		Block repeat;
		repeat.type = BT::RepeatN;
		repeat.count = 3;
		repeat.body = {message("tick..."), wait};
		countdown.push_back(repeat);
		countdown.push_back(message("Done!", true));
	}

	Document d;
	d.buttons.push_back({"Hello", {0.26f, 0.45f, 0.85f, 1}, hello});
	d.buttons.push_back({"Countdown", {0.20f, 0.55f, 0.25f, 1}, countdown});
	return d;
}
