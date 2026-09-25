// Child processes with captured output, and helpers to find / kill processes.
// Shared parts are in process.cpp, the OS parts in process_posix.cpp / process_win.cpp.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Exit status reported for a program AutoM8 killed (128 + SIGKILL, like a shell).
constexpr int kKilledExitStatus = 137;

struct Proc {
	std::string name;
	long long pid = -1;
	int exit_status = -1;   // -1 = has not exited yet
	std::deque<std::string> log;
	bool show_log = false;  // UI state
	uint64_t generation = 0; // bumped on every start

	Proc() = default;
	Proc(const Proc &) = delete; // owns OS handles
	Proc &operator=(const Proc &) = delete;
	~Proc();

	bool running() const { return pid > 0; }

	// Starts argv[0] (searched in PATH) with its own process group (POSIX) / job (Windows),
	// stdout+stderr captured, stdin empty. It keeps running if AutoM8 exits.
	bool start(const std::vector<std::string> &argv, const std::map<std::string, std::string> &env_over,
	           const std::string &cwd);
	// Same, through the system shell: sh -c on Linux, cmd /c on Windows.
	bool start_shell(const std::string &command, const std::map<std::string, std::string> &env_over,
	                 const std::string &cwd);
	void kill_now();  // kills the program and everything it started, and reaps it
	void terminate(); // asks politely: SIGTERM to the group / close its windows on Windows
	void poll();      // reads new output, notices exit
	void clear_log(); // empties `log`; line numbers keep counting

	size_t total_lines() const { return dropped + log.size(); } // lines ever added

	// Output of the current (or last) run only, as one string.
	std::string current_output() const;

	struct Os; // OS handles (pipe; on Windows also the process and job), defined per OS

private:
	Os *os = nullptr;
	std::string partial;
	size_t run_first_line = 0; // absolute line number where the current run starts
	size_t dropped = 0;        // lines dropped from the front of `log`

	void add_line(const std::string &l);
	void add_output(const char *data, size_t n); // splits into lines
	void read_output();
	void close_output();
	void started(long long new_pid); // bookkeeping after a successful spawn
	bool spawn(const std::vector<std::string> &argv, const std::string &shell_command,
	           const std::map<std::string, std::string> &env_over, const std::string &cwd);
};

// One scan of every process's command line (except us and our own shell wrappers).
class ProcessTable {
public:
	void refresh();
	// Processes whose command line contains one of the patterns.
	bool any(const std::vector<std::string> &patterns) const;
	// force: kill now; else ask to quit (SIGTERM / close windows). Returns how many were signalled.
	int kill(const std::vector<std::string> &patterns, bool force) const;

private:
	std::vector<std::pair<long long, std::string>> procs_;
	template <typename F> void for_matching(const std::vector<std::string> &patterns, F fn) const;
};

// Shell wrappers are skipped when matching: a pattern may just be text inside the script.
bool is_shell_wrapper(const std::string &cmdline);
bool signal_process(long long pid, bool force); // one process; false if it could not be signalled
