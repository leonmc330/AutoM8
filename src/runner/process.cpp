// The OS-independent half of process.hpp.
#include "process.hpp"

static constexpr size_t kMaxLogLines = 5000;

void Proc::add_line(const std::string &l)
{
	log.push_back(l);
	while (log.size() > kMaxLogLines) {
		log.pop_front();
		dropped++;
	}
}

void Proc::add_output(const char *data, size_t n)
{
	partial.append(data, n);
	size_t start = 0, pos;
	while ((pos = partial.find('\n', start)) != std::string::npos) {
		size_t end = pos > start && partial[pos - 1] == '\r' ? pos - 1 : pos; // Windows line endings
		add_line(partial.substr(start, end - start));
		start = pos + 1;
	}
	partial.erase(0, start);
}

void Proc::clear_log()
{
	dropped += log.size();
	log.clear();
}

bool Proc::start(const std::vector<std::string> &argv, const std::map<std::string, std::string> &env_over,
                 const std::string &cwd)
{
	if (running() || argv.empty()) return running();
	return spawn(argv, "", env_over, cwd);
}

bool Proc::start_shell(const std::string &command, const std::map<std::string, std::string> &env_over,
                       const std::string &cwd)
{
	if (running()) return true;
	return spawn({}, command, env_over, cwd);
}

void Proc::started(long long new_pid)
{
	pid = new_pid;
	exit_status = -1;
	generation++;
	add_line("---- started (pid " + std::to_string(pid) + ") ----");
	run_first_line = total_lines();
}

std::string Proc::current_output() const
{
	std::string s;
	size_t first = run_first_line > dropped ? run_first_line - dropped : 0;
	for (size_t i = first; i < log.size(); i++) {
		s += log[i];
		s += '\n';
	}
	return s;
}

template <typename F> void ProcessTable::for_matching(const std::vector<std::string> &patterns, F fn) const
{
	if (patterns.empty()) return;
	for (auto &pc : procs_)
		for (auto &pat : patterns)
			if (pc.second.find(pat) != std::string::npos) {
				fn(pc.first);
				break;
			}
}

bool ProcessTable::any(const std::vector<std::string> &patterns) const
{
	bool found = false;
	for_matching(patterns, [&](long long) { found = true; });
	return found;
}

int ProcessTable::kill(const std::vector<std::string> &patterns, bool force) const
{
	int n = 0;
	for_matching(patterns, [&](long long p) {
		if (signal_process(p, force)) n++;
	});
	return n;
}
