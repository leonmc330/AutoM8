// The document model: buttons, each with a sequence of blocks; JSON load/save.
#pragma once

#include "nlohmann/json.hpp"

#include <string>
#include <vector>

using json = nlohmann::json;

enum class BT {
	Run, WaitSeconds, WaitUntil, If, RepeatN, RepeatUntil,
	KillProgram, KillMatching, KillAll, Message, Stop, Throw
};

struct Color {
	float r, g, b, a;
};

struct BlockInfo {
	BT type;
	const char *id;    // name in the JSON file
	const char *label; // name in the editor
	Color color;
};

extern const BlockInfo kBlocks[];
extern const int kBlockCount;
const BlockInfo &info(BT t);

enum class CT { OutputMatches, ProgramRunning, ProcessRunning, FileExists, CommandSucceeds, ExitCodeIs, UserSaysYes };
extern const char *const kCondLabels[];
extern const int kCondCount;

struct Condition {
	CT type = CT::ProgramRunning;
	bool negate = false;
	std::string program; // program name (output / running / exit code)
	std::string text;    // regex / patterns / path / command / question
	int number = 0;      // exit code
};

struct Block {
	BT type = BT::Message;
	// Run
	std::string name, command, cwd, env, match;
	bool shell = false, blocking = false, skip_if_running = true;
	float min_s = 0, max_s = -1;
	// waits, loops, kills, messages
	float seconds = 1;    // Wait seconds; Wait until timeout (-1 = forever)
	float graceful_s = 0; // Kill blocks: SIGTERM first, SIGKILL after this long (0 = SIGKILL now)
	int count = 3;        // Repeat N times (-1 = forever)
	bool stop_on_timeout = false;
	bool popup = false;   // Show message
	Condition cond;
	std::vector<Block> body, else_body; // If then/else, loop bodies
};
using Seq = std::vector<Block>;

struct Button {
	std::string name = "Button";
	Color color = {0.26f, 0.45f, 0.85f, 1};
	Seq seq;
};

struct Document {
	std::vector<Button> buttons;

	Button *find(const std::string &name);
};

bool load_document(const std::string &path, Document &doc, std::string *error = nullptr);
bool save_document(const std::string &path, const Document &doc); // atomic: the old file survives a failure
std::string document_json(const Document &doc); // what save_document writes
Document default_document(); // a small example, written on first run
