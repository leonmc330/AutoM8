// The document model: buttons, each with a sequence of blocks; JSON load/save.
#pragma once

#include "nlohmann/json.hpp"
#include "values.hpp"

#include <string>
#include <vector>

using json = nlohmann::json;

enum class BT {
	Run, WaitSeconds, WaitUntil, If, RepeatN, RepeatUntil, SetValue, Operate,
	KillProgram, KillMatching, KillAll, Message, Stop, Throw
};

struct Color {
	float r, g, b, a;
};

struct BlockInfo {
	BT type;
	const char *id;    // name in the JSON file
	const char *label; // name in the editor
	const char *family; // submenu of "+ add block"; kBlocks keeps each family together
	Color color;
};

extern const BlockInfo kBlocks[];
extern const int kBlockCount;
const BlockInfo &info(BT t);

enum class CT {
	OutputMatches, ProgramRunning, ProcessRunning, FileExists, CommandSucceeds, ExitCodeIs, UserSaysYes,
	Compare, ValueTrue
};
extern const char *const kCondLabels[];
extern const int kCondCount;

struct Condition {
	CT type = CT::ProgramRunning;
	bool negate = false;
	std::string program; // program name (output / running / exit code)
	std::string text;    // regex / patterns / path / command / question
	int number = 0;      // exit code
	std::string left, right; // Compare: operands (see evaluate()); ValueTrue: `left`
	Cmp cmp = Cmp::Eq;
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
	// Set value (`name` = the value, `command` = number / text as typed, `flag` = yes/no)
	VK kind = VK::Number;
	bool flag = false;
	// Operate: name = left OP right
	std::string left, right;
	Op op = Op::Add;
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

// Every file carries "version": the format it was written in (files from before it are version 1).
// A program reads kMinDocumentVersion..kDocumentVersion and refuses the others, so it never
// runs or saves over a file whose format it doesn't know. Bump kDocumentVersion when the format
// changes; raise kMinDocumentVersion only when older files can't be read anymore.
constexpr int kDocumentVersion = 2; // 2: values (Set value, Operate, compare conditions)
constexpr int kMinDocumentVersion = 1;

// false: `error` says why, `incompatible` is set when the file's version is the reason.
bool load_document(const std::string &path, Document &doc, std::string *error = nullptr, bool *incompatible = nullptr);
bool save_document(const std::string &path, const Document &doc); // atomic: the old file survives a failure
std::string document_json(const Document &doc); // what save_document writes
Document default_document(); // a small example, written on first run
