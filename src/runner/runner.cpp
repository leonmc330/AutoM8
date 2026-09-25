#include "runner.hpp"

#include <filesystem>

namespace fs = std::filesystem;

static constexpr size_t kMaxHistory = 500;
static constexpr double kProcessScanSeconds = 0.25; // how long a /proc scan is reused

static std::vector<std::string> patterns(const Block &b) { return split_list(b.match, ','); }

static void collect(const Seq &s, std::vector<const Block *> &out)
{
	for (auto &b : s) {
		if (b.type == BT::Run && !b.name.empty()) out.push_back(&b);
		collect(b.body, out);
		collect(b.else_body, out);
	}
}

static long long mtime_of(const std::string &path)
{
	std::error_code ec;
	auto t = fs::last_write_time(path_of(path), ec);
	return ec ? 0 : (long long)t.time_since_epoch().count();
}

void Runner::say(const std::string &msg)
{
	status = msg;
	history.push_back(msg);
	while (history.size() > kMaxHistory) {
		history.pop_front();
		history_start++;
	}
}

void Runner::set_doc(Document d)
{
	doc = std::move(d);
	programs_.clear();
	for (auto &b : doc.buttons) collect(b.seq, programs_);
	regexes_.clear();
	output_matches_.clear();
}

Runner::Runner(const std::string &p) : path(p)
{
	cond_proc.name = "(condition)";
	Document d;
	std::string err;
	if (load_document(path, d, &err)) {
		set_doc(std::move(d));
	} else if (std::error_code ec; fs::exists(path_of(path), ec)) {
		load_error = err;
		say("Cannot read " + path + ": " + err);
	} else {
		set_doc(default_document()); // first run: create the file
		if (save_document(path, doc)) say("Created " + path);
		else say("Cannot create " + path);
	}
	file_mtime = mtime_of(path);
	last_check = Clock::now();
}

void Runner::reload_if_changed()
{
	if (active || secs_since(last_check) < 1.0) return;
	last_check = Clock::now();
	long long m = mtime_of(path);
	if (m == file_mtime) return;
	file_mtime = m;
	Document d;
	std::string err;
	if (load_document(path, d, &err)) {
		set_doc(std::move(d));
		say("Reloaded " + path_string(path_of(path).filename()));
	} else {
		say("Reload failed: " + err);
	}
}

const ProcessTable &Runner::processes()
{
	if (!table_valid_ || secs_since(table_time_) >= kProcessScanSeconds) {
		table_.refresh();
		table_time_ = Clock::now();
		table_valid_ = true;
	}
	return table_;
}

const Block *Runner::program(const std::string &name) const
{
	for (auto *b : programs_)
		if (b->name == name) return b;
	return nullptr;
}

bool Runner::program_running(const std::string &name)
{
	auto it = procs.find(name);
	if (it != procs.end() && it->second.running()) return true;
	const Block *b = program(name);
	return b && processes().any(patterns(*b));
}

int Runner::kill_program(const std::string &name, bool force)
{
	int n = 0;
	auto it = procs.find(name);
	if (it != procs.end() && it->second.running()) {
		if (force) it->second.kill_now();
		else it->second.terminate();
		n++;
	}
	if (const Block *b = program(name)) n += processes().kill(patterns(*b), force);
	table_valid_ = false;
	return n;
}

// Returns false when nothing was started (already running, or failed).
bool Runner::start_program(const Block &b)
{
	Proc &p = procs[b.name];
	p.name = b.name;
	if (b.skip_if_running && (p.running() || processes().any(patterns(b)))) return false;

	std::vector<std::string> argv;
	if (!b.shell)
		for (auto &a : split_command(b.command)) argv.push_back(expand(a));
	std::map<std::string, std::string> env;
	for (auto &line : split_list(b.env, '\n')) {
		size_t eq = line.find('=');
		if (eq != std::string::npos) env[trim(line.substr(0, eq))] = expand(trim(line.substr(eq + 1)));
	}
	bool ok = b.shell ? p.start_shell(b.command, env, expand(b.cwd)) : !argv.empty() && p.start(argv, env, expand(b.cwd));
	if (!ok) {
		say("Cannot start " + b.name);
		return false;
	}
	table_valid_ = false;
	return true;
}

void Runner::press(const std::string &name)
{
	Button *b = doc.find(name);
	if (!b) {
		say("No button named '" + name + "'");
		return;
	}
	if (cond_proc.running()) cond_proc.kill_now();
	cond_running = false;
	active = true;
	running_button = name;
	stack = {{&b->seq, 0}};
	started = false;
	loops.clear();
	asking = ask_open = false;
	say(name + ": running");
}

int Runner::output_matches(const Condition &c)
{
	auto it = procs.find(c.program);
	if (it == procs.end()) return 0;
	const Proc &p = it->second;

	// Only search again when the program printed something new.
	std::string key = c.program + '\n' + c.text;
	auto cached = output_matches_.find(key);
	if (cached != output_matches_.end() && cached->second.generation == p.generation &&
	    cached->second.lines == p.total_lines())
		return cached->second.result;

	auto &re = regexes_[c.text];
	if (!re) {
		re = std::make_unique<Regex>(c.text);
		if (!re->ok()) say("Bad regex '" + c.text + "': " + re->error());
	}
	int r = re->search(p.current_output());
	if (r < 0) {
		say("Regex '" + c.text + "' is too slow for the output of " + c.program);
		r = 0;
	}
	output_matches_[key] = {p.generation, p.total_lines(), r};
	return r;
}

int Runner::eval(const Condition &c, bool first)
{
	int r = 0;
	switch (c.type) {
	case CT::OutputMatches: r = output_matches(c); break;
	case CT::ProgramRunning: r = program_running(c.program); break;
	case CT::ProcessRunning: r = processes().any(split_list(c.text, ',')); break;
	case CT::FileExists: {
		std::error_code ec;
		r = fs::exists(path_of(expand(c.text)), ec);
		break;
	}
	case CT::CommandSucceeds:
		if (first || !cond_running) {
			cond_proc.kill_now(); // a command left over from an earlier block (timed out) is not ours
			cond_proc.clear_log();
			cond_running = cond_proc.start_shell(c.text, {}, "");
			if (!cond_running) return c.negate ? 1 : 0;
			return -1;
		}
		if (cond_proc.running()) return -1;
		cond_running = false;
		r = cond_proc.exit_status == 0;
		break;
	case CT::ExitCodeIs: {
		auto it = procs.find(c.program);
		r = it != procs.end() && !it->second.running() && it->second.exit_status == c.number;
		break;
	}
	case CT::UserSaysYes:
		if (first) {
			ask_text = c.text;
			ask_answer = -1;
			asking = ask_open = true;
			return -1;
		}
		if (ask_answer < 0) return -1;
		r = ask_answer == 1;
		ask_answer = -1;
		asking = false;
		break;
	}
	return c.negate ? !r : r;
}

// Kill blocks: optional polite phase (SIGTERM / close windows), then a hard kill. Returns true when done.
bool Runner::kill_step(const Block &b, bool first)
{
	auto targets_running = [&] {
		if (b.type == BT::KillProgram) return program_running(b.name);
		if (b.type == BT::KillMatching) return processes().any(split_list(b.match, ','));
		for (auto *p : programs_)
			if (program_running(p->name)) return true;
		return false;
	};
	auto send = [&](bool force) {
		if (b.type == BT::KillProgram) {
			kill_program(b.name, force);
		} else if (b.type == BT::KillMatching) {
			processes().kill(split_list(b.match, ','), force);
			table_valid_ = false;
		} else {
			for (auto *p : programs_) kill_program(p->name, force);
		}
	};
	if (b.graceful_s <= 0) {
		send(true);
		return true;
	}
	if (first) {
		send(false);
		return false;
	}
	if (!targets_running()) return true;
	if (secs_since(t0) >= b.graceful_s) {
		send(true);
		return true;
	}
	return false;
}

bool Runner::step(Block &b, bool first)
{
	switch (b.type) {
	case BT::Run: {
		if (first) {
			say("Running " + b.name);
			bool launched = start_program(b);
			run_generation = launched ? procs[b.name].generation : 0;
			if (!b.blocking) return true;
			if (!launched) return secs_since(t0) >= b.min_s;
		}
		Proc &p = procs[b.name];
		bool still_running = p.running() && p.generation == run_generation;
		double t = secs_since(t0);
		if (b.max_s >= 0 && t >= b.max_s) return true; // max reached: move on, it keeps running
		return !still_running && t >= b.min_s;        // exited, and the minimum time has passed
	}
	case BT::WaitSeconds: return secs_since(t0) >= b.seconds;
	case BT::WaitUntil: {
		if (eval(b.cond, first) == 1) return true;
		if (b.seconds >= 0 && secs_since(t0) >= b.seconds) {
			asking = false; // a question nobody answered in time
			if (b.stop_on_timeout) {
				say(running_button + ": stopped, 'Wait until' timed out");
				active = false;
			}
			return true;
		}
		return false;
	}
	case BT::KillProgram:
	case BT::KillMatching:
	case BT::KillAll: return kill_step(b, first);
	case BT::Message:
		say(b.command);
		if (b.popup) popup_text = b.command;
		return true;
	case BT::Stop:
		say(running_button + ": stopped by a block");
		active = false;
		return true;
	case BT::Throw:
		error_text = b.command.empty() ? "Error" : b.command;
		say(running_button + " failed: " + error_text);
		active = false;
		return true;
	case BT::If:
	case BT::RepeatN:
	case BT::RepeatUntil: return true; // handled in update()
	}
	return true;
}

void Runner::update()
{
	for (auto &kv : procs) kv.second.poll();
	cond_proc.poll();

	for (int guard = 0; guard < 64 && active; guard++) {
		while (!stack.empty() && stack.back().idx >= stack.back().seq->size()) stack.pop_back();
		if (stack.empty()) {
			active = false;
			say(running_button + ": done");
			break;
		}

		Frame &f = stack.back();
		Block &b = (*f.seq)[f.idx];
		bool first = !started;
		if (first) t0 = Clock::now();

		if (b.type == BT::If || b.type == BT::RepeatUntil) {
			int r = eval(b.cond, first);
			if (r < 0) { // still evaluating (question, shell command...)
				started = true;
				break;
			}
			started = false;
			if (b.type == BT::If) {
				f.idx++;
				Seq &branch = r ? b.body : b.else_body;
				if (!branch.empty()) stack.push_back({&branch, 0}); // `f` is not used after this
			} else if (r || b.body.empty()) {
				f.idx++; // condition met: leave the loop
			} else {
				stack.push_back({&b.body, 0}); // run the body, then come back to this block
			}
			continue;
		}
		if (b.type == BT::RepeatN) {
			started = false;
			int &done_count = loops[&b];
			if (b.body.empty() || (b.count >= 0 && done_count >= b.count)) {
				loops.erase(&b);
				f.idx++;
			} else {
				done_count++;
				stack.push_back({&b.body, 0});
			}
			continue;
		}

		bool done = step(b, first);
		if (!active) break;
		if (done) {
			stack.back().idx++;
			started = false;
			continue;
		}
		started = true;
		break; // waiting: resume next time
	}
}
