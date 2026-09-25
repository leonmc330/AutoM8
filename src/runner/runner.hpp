// Runs a button's sequence block by block without blocking: update() is called
// in a loop and advances as far as it can.
#pragma once

#include "blocks.hpp"
#include "process.hpp"
#include "regex.hpp"
#include "util.hpp"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Runner {
	std::string path; // the sequence file
	Document doc;
	std::map<std::string, Proc> procs; // programs started by Run blocks, by name
	std::string status = "Ready.";
	std::deque<std::string> history; // the latest status messages, in order
	size_t history_start = 0;        // how many older messages were dropped from `history`

	// State of the running sequence (read by the window / terminal).
	bool active = false;
	std::string running_button; // name of the button being run
	bool asking = false;        // a "user answers Yes" question is waiting for an answer
	bool ask_open = false;      // ... and the UI has not shown it yet
	int ask_answer = -1;        // 1 yes, 0 no
	std::string ask_text;
	std::string popup_text; // Show message (popup)
	std::string error_text; // Throw error

	explicit Runner(const std::string &path);
	void reload_if_changed(); // picks up edits saved by the editor (only when idle)

	void press(const std::string &button); // cancels a running sequence, runs this one
	void update();

	const std::vector<const Block *> &programs() const { return programs_; } // every named Run block
	bool program_running(const std::string &name);
	int kill_program(const std::string &name, bool force = true); // force: kill now, else ask to quit

private:
	void say(const std::string &msg); // sets status and appends to history
	void set_doc(Document d);
	const ProcessTable &processes(); // /proc scan, reused for a short while

	struct Frame {
		Seq *seq;
		size_t idx;
	};
	std::vector<Frame> stack;
	bool started = false;       // current block already started
	Clock::time_point t0;       // when the current block started
	std::map<const Block *, int> loops; // Repeat N counters
	Proc cond_proc;             // runs "shell command succeeds" conditions
	bool cond_running = false;
	uint64_t run_generation = 0; // generation of the program the current Run block started
	long long file_mtime = 0;
	Clock::time_point last_check;

	std::vector<const Block *> programs_;
	ProcessTable table_;
	Clock::time_point table_time_;
	bool table_valid_ = false;

	std::map<std::string, std::unique_ptr<Regex>> regexes_; // compiled once per pattern
	struct OutputMatch {
		uint64_t generation;
		size_t lines;
		int result;
	};
	std::map<std::string, OutputMatch> output_matches_; // last result per program + regex

	const Block *program(const std::string &name) const;
	bool start_program(const Block &b);
	int output_matches(const Condition &c);
	int eval(const Condition &c, bool first); // -1 = not decided yet, 0 / 1
	bool step(Block &b, bool first);          // true = block finished
	bool kill_step(const Block &b, bool first);
};
