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
#include <set>
#include <string>
#include <vector>

struct Runner {
	std::string path; // the sequence file
	std::string load_error; // why the file could not be read ("" = it was read, or just created)
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
	Values values;          // set by the running sequence; emptied when it ends or another one starts

	explicit Runner(const std::string &path);
	void reload_if_changed(); // picks up edits saved by the editor (only when idle)

	void press(const std::string &button); // cancels a running sequence, runs this one
	void update();

	const std::vector<const Block *> &programs() const { return programs_; } // every named Run block
	bool program_running(const std::string &name);
	int kill_program(const std::string &name, bool force = true); // force: kill now, else ask to quit

private:
	struct Thread; // below
	void say(const std::string &msg); // sets status and appends to history
	void fail(const std::string &msg); // ends the sequence as an error, like Throw error
	bool value_of(const std::string &operand, Value &out); // false: failed
	const Values &visible_values(); // what the current thread sees: shared values + its index values
	void set_value(const std::string &name, Value v);
	std::string prog_name(const std::string &name) const; // the current thread's copy of a program
	void end_thread(Thread &t, const std::string &why); // `why` "" = quietly
	void start_threads(Block &b);
	bool threads_left(int parent, const Block *group) const;
	bool advance(Thread &t); // false: the sequence ended (error / stop)
	void set_doc(Document d);
	const ProcessTable &processes(); // /proc scan, reused for a short while

	struct Frame {
		Seq *seq;
		size_t idx;
	};
	// One line of execution: the button's sequence, or one copy of a Thread block's body.
	// They take turns in update(); values are shared, except the thread's index values.
	struct Thread {
		int id = 0, parent = -1;          // parent: the thread that ran the Thread block
		const Block *group = nullptr;     // that Thread block (a blocking one waits for its threads)
		std::string label;                // "#2", "#2.3" in a thread inside a thread; "" = the button's sequence
		std::set<std::string> own;        // Run blocks inside the Thread block: each thread has its own copy
		Values locals;                    // index values: each thread sees its own number
		Clock::time_point born;
		float max_s = -1;                 // killed after this long (-1 = no limit)
		bool kill_programs = false;       // ... and its programs with it
		bool done = false;
		std::vector<Frame> stack;
		bool started = false;             // current block already started
		Clock::time_point t0;             // when the current block started
		std::map<const Block *, int> loops; // Repeat N counters
		Proc cond_proc;                   // runs "shell command succeeds" conditions
		bool cond_running = false;
		int ask_state = 0;                // "user answers Yes": 0 not asked yet, 1 asking, 2 answered
		uint64_t run_generation = 0;      // generation of the program the current Run block started
	};
	std::vector<std::unique_ptr<Thread>> threads_; // [0] = the button's sequence
	Thread *cur_ = nullptr;                          // the thread update() is advancing
	const Thread *ask_owner_ = nullptr;              // the thread whose question is shown
	int next_thread_id_ = 0;
	long long file_mtime = 0;
	Clock::time_point last_check;

	std::vector<const Block *> programs_;
	ProcessTable table_;
	Clock::time_point table_time_;
	bool table_valid_ = false;

	Values merged_; // visible_values() of a thread with index values

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
