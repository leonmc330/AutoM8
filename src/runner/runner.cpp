#include "runner.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static constexpr size_t kMaxHistory = 500;
static constexpr double kProcessScanSeconds = 0.25; // how long a /proc scan is reused
static constexpr int kMaxThreads = 256; // per Thread block

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

void Runner::fail(const std::string &msg)
{
	error_text = msg.empty() ? "Error" : msg;
	say(running_button + " failed: " + error_text);
	active = false;
}

bool Runner::value_of(const std::string &operand, Value &out)
{
	std::string err;
	if (evaluate(operand, visible_values(), out, err)) return true;
	fail(err);
	return false;
}

const Values &Runner::visible_values()
{
	if (!cur_ || cur_->locals.empty()) return values;
	merged_ = values;
	for (auto &kv : cur_->locals) merged_[kv.first] = kv.second;
	return merged_;
}

// A thread's index value stays its own; every other value is shared by all threads.
void Runner::set_value(const std::string &name, Value v)
{
	if (cur_ && cur_->locals.count(name)) cur_->locals[name] = std::move(v);
	else values[name] = std::move(v);
}

std::string Runner::prog_name(const std::string &name) const
{
	return cur_ && cur_->own.count(name) ? name + " " + cur_->label : name;
}

// Calls fn on the program `name` and on every thread's copy of it ("name #2", "name #2.1").
template <typename F> static void for_copies(std::map<std::string, Proc> &procs, const std::string &name, F fn)
{
	for (auto it = procs.lower_bound(name); it != procs.end() && it->first.compare(0, name.size(), name) == 0; ++it)
		if (it->first.size() == name.size() || it->first.compare(name.size(), 2, " #") == 0) fn(it->second);
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
	bool running = false;
	for_copies(procs, name, [&](Proc &p) { running |= p.running(); });
	if (running) return true;
	const Block *b = program(name);
	return b && processes().any(patterns(*b));
}

int Runner::kill_program(const std::string &name, bool force)
{
	int n = 0;
	for_copies(procs, name, [&](Proc &p) {
		if (!p.running()) return;
		if (force) p.kill_now();
		else p.terminate();
		n++;
	});
	if (const Block *b = program(name)) n += processes().kill(patterns(*b), force);
	table_valid_ = false;
	return n;
}

// Returns false when nothing was started (already running, or failed).
bool Runner::start_program(const Block &b)
{
	std::string key = prog_name(b.name);
	Proc &p = procs[key];
	p.name = key;
	// A thread's copy only checks itself: the match patterns would find the other threads' copies.
	if (b.skip_if_running && (p.running() || (key == b.name && processes().any(patterns(b))))) return false;

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
		say("Cannot start " + key);
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
	for (auto &t : threads_)
		if (t->cond_proc.running()) t->cond_proc.kill_now();
	threads_.clear();
	auto main = std::make_unique<Thread>();
	main->cond_proc.name = "(condition)";
	main->born = Clock::now();
	main->stack = {{&b->seq, 0}};
	threads_.push_back(std::move(main));
	next_thread_id_ = 1;
	cur_ = nullptr;
	active = true;
	running_button = name;
	values.clear();
	asking = ask_open = false;
	ask_owner_ = nullptr;
	say(name + ": running");
}

int Runner::output_matches(const Condition &c)
{
	std::string name = prog_name(c.program);
	auto it = procs.find(name);
	if (it == procs.end()) return 0;
	const Proc &p = it->second;

	// Only search again when the program printed something new.
	std::string key = name + '\n' + c.text;
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
		say("Regex '" + c.text + "' is too slow for the output of " + name);
		r = 0;
	}
	output_matches_[key] = {p.generation, p.total_lines(), r};
	return r;
}

int Runner::eval(const Condition &c, bool first)
{
	Thread &t = *cur_;
	int r = 0;
	switch (c.type) {
	case CT::OutputMatches: r = output_matches(c); break;
	case CT::ProgramRunning: r = program_running(prog_name(c.program)); break;
	case CT::ProcessRunning: r = processes().any(split_list(c.text, ',')); break;
	case CT::FileExists: {
		std::error_code ec;
		r = fs::exists(path_of(expand(c.text)), ec);
		break;
	}
	case CT::CommandSucceeds:
		if (first || !t.cond_running) {
			t.cond_proc.kill_now(); // a command left over from an earlier block (timed out) is not ours
			t.cond_proc.clear_log();
			t.cond_running = t.cond_proc.start_shell(c.text, {}, "");
			if (!t.cond_running) return c.negate ? 1 : 0;
			return -1;
		}
		if (t.cond_proc.running()) return -1;
		t.cond_running = false;
		r = t.cond_proc.exit_status == 0;
		break;
	case CT::ExitCodeIs: {
		auto it = procs.find(prog_name(c.program));
		r = it != procs.end() && !it->second.running() && it->second.exit_status == c.number;
		break;
	}
	case CT::UserSaysYes:
		if (first) t.ask_state = 0;
		if (t.ask_state == 0) {
			if (asking) return -1; // another thread's question first
			ask_text = substitute(c.text, visible_values());
			ask_answer = -1;
			asking = ask_open = true;
			ask_owner_ = &t;
			t.ask_state = 1;
			return -1;
		}
		if (ask_owner_ != &t || ask_answer < 0) return -1;
		r = ask_answer == 1;
		ask_answer = -1;
		asking = false;
		ask_owner_ = nullptr;
		t.ask_state = 2;
		break;
	case CT::Compare: {
		Value a, b;
		std::string err;
		bool out = false;
		if (!value_of(c.left, a) || !value_of(c.right, b)) return 0;
		if (!compare(c.cmp, a, b, out, err)) {
			fail(err);
			return 0;
		}
		r = out;
		break;
	}
	case CT::ValueTrue: {
		Value v;
		if (!value_of(c.left, v)) return 0;
		if (kind_of(v) != VK::Bool) {
			fail("'" + trim(c.left) + "' is a " + kKindLabels[v.index()] + ", not a yes/no");
			return 0;
		}
		r = std::get<bool>(v);
		break;
	}
	}
	return c.negate ? !r : r;
}

// Kill blocks: optional polite phase (SIGTERM / close windows), then a hard kill. Returns true when done.
bool Runner::kill_step(const Block &b, bool first)
{
	auto targets_running = [&] {
		if (b.type == BT::KillProgram) return program_running(prog_name(b.name));
		if (b.type == BT::KillMatching) return processes().any(split_list(b.match, ','));
		for (auto *p : programs_)
			if (program_running(p->name)) return true;
		return false;
	};
	auto send = [&](bool force) {
		if (b.type == BT::KillProgram) {
			kill_program(prog_name(b.name), force);
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
	if (secs_since(cur_->t0) >= b.graceful_s) {
		send(true);
		return true;
	}
	return false;
}

void Runner::end_thread(Thread &t, const std::string &why)
{
	if (t.done) return;
	t.done = true;
	if (!why.empty()) say("Thread " + t.label + ": " + why);
	if (t.cond_proc.running()) t.cond_proc.kill_now();
	if (ask_owner_ == &t) { // its question goes away with it
		asking = false;
		ask_owner_ = nullptr;
	}
}

static const char *const kStoppedByBlock = "stopped by a block";

void Runner::start_threads(Block &b)
{
	int n = std::min(b.count, kMaxThreads);
	if (n <= 0) return;
	std::vector<const Block *> runs;
	collect(b.body, runs);
	std::string index = trim(b.name);
	say("Starting " + std::to_string(n) + (n == 1 ? " thread" : " threads"));
	for (int k = 1; k <= n; k++) {
		auto t = std::make_unique<Thread>();
		t->id = next_thread_id_++;
		t->parent = cur_->id;
		t->group = &b;
		t->label = (cur_->label.empty() ? "#" : cur_->label + ".") + std::to_string(k);
		t->own = cur_->own;
		for (auto *r : runs) t->own.insert(r->name);
		t->locals = cur_->locals;
		if (!index.empty()) t->locals[index] = (double)k;
		t->born = Clock::now();
		t->max_s = b.max_s;
		t->stack = {{&b.body, 0}};
		t->cond_proc.name = "(condition)";
		threads_.push_back(std::move(t)); // cur_ stays valid: threads are owned by unique_ptr
	}
}

bool Runner::threads_left(int parent, const Block *group) const
{
	for (auto &t : threads_)
		if (!t->done && t->parent == parent && t->group == group) return true;
	return false;
}

bool Runner::step(Block &b, bool first)
{
	Thread &t = *cur_;
	switch (b.type) {
	case BT::Run: {
		std::string key = prog_name(b.name);
		if (first) {
			say("Running " + key);
			bool launched = start_program(b);
			t.run_generation = launched ? procs[key].generation : 0;
			if (!b.blocking) return true;
			if (!launched) return secs_since(t.t0) >= b.min_s;
		}
		Proc &p = procs[key];
		bool still_running = p.running() && p.generation == t.run_generation;
		double secs = secs_since(t.t0);
		if (b.max_s >= 0 && secs >= b.max_s) return true; // max reached: move on, it keeps running
		return !still_running && secs >= b.min_s;        // exited, and the minimum time has passed
	}
	case BT::WaitSeconds: return secs_since(t.t0) >= b.seconds;
	case BT::WaitUntil: {
		if (eval(b.cond, first) == 1) return true;
		if (b.seconds >= 0 && secs_since(t.t0) >= b.seconds) {
			if (ask_owner_ == &t) { // a question nobody answered in time
				asking = false;
				ask_owner_ = nullptr;
			}
			if (b.stop_on_timeout) {
				if (!t.label.empty()) {
					end_thread(t, "stopped, 'Wait until' timed out");
				} else {
					say(running_button + ": stopped, 'Wait until' timed out");
					active = false;
				}
			}
			return true;
		}
		return false;
	}
	case BT::KillProgram:
	case BT::KillMatching:
	case BT::KillAll: return kill_step(b, first);
	case BT::SetValue: {
		double d;
		if (b.name.empty()) fail("Set value: the value has no name");
		else if (b.kind == VK::Bool) set_value(b.name, b.flag);
		else if (b.kind == VK::Text) set_value(b.name, b.command);
		else if (parse_number(b.command, d)) set_value(b.name, d);
		else fail("Set value " + b.name + ": '" + b.command + "' is not a number");
		return true;
	}
	case BT::Operate: {
		Value l, r = false, out;
		std::string err;
		if (b.name.empty()) fail("Operate: the result has no name");
		else if (!value_of(b.left, l) || (b.op != Op::Not && !value_of(b.right, r))) return true;
		else if (!operate(b.op, l, r, out, err)) fail(b.name + ": " + err);
		else set_value(b.name, std::move(out));
		return true;
	}
	case BT::Message: {
		std::string text = substitute(b.command, visible_values());
		say(text);
		if (b.popup) popup_text = text;
		return true;
	}
	case BT::Stop:
		if (!t.label.empty()) {
			end_thread(t, kStoppedByBlock);
		} else {
			say(running_button + ": " + kStoppedByBlock);
			active = false;
		}
		return true;
	case BT::Throw: fail(substitute(b.command, visible_values())); return true;
	case BT::Thread:
		if (first) {
			start_threads(b);
			if (!b.blocking) return true;
		}
		return !threads_left(t.id, &b);
	case BT::If:
	case BT::RepeatN:
	case BT::RepeatUntil: return true; // handled in advance()
	}
	return true;
}

bool Runner::advance(Thread &t)
{
	cur_ = &t;
	for (int guard = 0; guard < 64 && active && !t.done; guard++) {
		while (!t.stack.empty() && t.stack.back().idx >= t.stack.back().seq->size()) t.stack.pop_back();
		if (t.stack.empty()) {
			t.done = true;
			break;
		}

		Frame &f = t.stack.back();
		Block &b = (*f.seq)[f.idx];
		bool first = !t.started;
		if (first) t.t0 = Clock::now();

		if (b.type == BT::If || b.type == BT::RepeatUntil) {
			int r = eval(b.cond, first);
			if (!active) break; // the condition failed (unknown value...)
			if (r < 0) { // still evaluating (question, shell command...)
				t.started = true;
				break;
			}
			t.started = false;
			if (b.type == BT::If) {
				f.idx++;
				Seq &branch = r ? b.body : b.else_body;
				if (!branch.empty()) t.stack.push_back({&branch, 0}); // `f` is not used after this
			} else if (r || b.body.empty()) {
				f.idx++; // condition met: leave the loop
			} else {
				t.stack.push_back({&b.body, 0}); // run the body, then come back to this block
			}
			continue;
		}
		if (b.type == BT::RepeatN) {
			t.started = false;
			int &done_count = t.loops[&b];
			if (b.body.empty() || (b.count >= 0 && done_count >= b.count)) {
				t.loops.erase(&b);
				f.idx++;
			} else {
				done_count++;
				t.stack.push_back({&b.body, 0});
			}
			continue;
		}

		bool done = step(b, first);
		if (!active || t.done) break;
		if (done) {
			t.stack.back().idx++;
			t.started = false;
			continue;
		}
		t.started = true;
		break; // waiting: resume next time
	}
	cur_ = nullptr;
	return active;
}

// Kills a thread and the threads it started, and theirs.
static void end_tree(std::vector<int> &ids, const std::vector<std::pair<int, int>> &parents, int id)
{
	ids.push_back(id);
	for (auto &p : parents)
		if (p.second == id) end_tree(ids, parents, p.first);
}

void Runner::update()
{
	for (auto &kv : procs) kv.second.poll();
	for (auto &t : threads_) t->cond_proc.poll();

	// Each thread in turn; threads started meanwhile get their first turn in this same pass.
	for (size_t i = 0; i < threads_.size() && active; i++) {
		Thread &t = *threads_[i];
		if (t.done) continue;
		if (t.max_s >= 0 && secs_since(t.born) >= t.max_s) {
			std::vector<std::pair<int, int>> parents;
			for (auto &o : threads_) parents.push_back({o->id, o->parent});
			std::vector<int> ids;
			end_tree(ids, parents, t.id);
			char secs[32];
			snprintf(secs, sizeof secs, "%g", t.max_s);
			end_thread(t, std::string("killed after ") + secs + " s");
			for (auto &o : threads_)
				if (std::count(ids.begin(), ids.end(), o->id)) end_thread(*o, "");
			continue;
		}
		advance(t);
	}

	if (active) {
		bool left = false;
		for (auto &t : threads_) left |= !t->done;
		if (!left) {
			active = false;
			say(running_button + ": done");
		}
	}
	if (active) { // forget finished threads (the button's own sequence stays first)
		threads_.erase(std::remove_if(threads_.begin() + 1, threads_.end(), [](auto &t) { return t->done; }),
		               threads_.end());
	} else {
		for (auto &t : threads_) end_thread(*t, "");
		values.clear(); // they only live while their sequence runs
	}
}
