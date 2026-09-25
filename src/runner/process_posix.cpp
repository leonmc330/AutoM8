// process.hpp on Linux (and other POSIX systems with /proc).
#include "process.hpp"
#include "util.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
extern char **environ;

struct Proc::Os {
	int out_fd = -1;
};

Proc::~Proc()
{
	close_output();
	delete os;
}

// Only async-signal-safe calls between fork() and exec().
static void child_write(const char *a, const char *b = "", const char *c = "")
{
	for (const char *s : {a, b, c})
		if (write(STDERR_FILENO, s, strlen(s)) < 0) return;
}

bool Proc::spawn(const std::vector<std::string> &argv_in, const std::string &shell_command,
                 const std::map<std::string, std::string> &env_over, const std::string &cwd)
{
	if (!os) os = new Os;
	std::vector<std::string> argv = shell_command.empty() && !argv_in.empty()
	                                    ? argv_in
	                                    : std::vector<std::string>{"/bin/sh", "-c", shell_command};

	// Everything the child needs is built before fork(): allocating after it is unsafe
	// in a program with threads (SDL starts some).
	std::map<std::string, std::string> env;
	for (char **e = environ; *e; e++) {
		const char *eq = strchr(*e, '=');
		if (eq) env[std::string(*e, eq - *e)] = eq + 1;
	}
	for (auto &kv : env_over) env[kv.first] = kv.second;
	std::vector<std::string> env_s;
	for (auto &kv : env) env_s.push_back(kv.first + "=" + kv.second);
	std::vector<char *> a, e;
	for (auto &s : argv) a.push_back(const_cast<char *>(s.c_str()));
	a.push_back(nullptr);
	for (auto &s : env_s) e.push_back(const_cast<char *>(s.c_str()));
	e.push_back(nullptr);

	// Output still buffered from the previous run belongs to it; then drop its pipe
	// (a grandchild that left the process group may still hold it open).
	read_output();
	close_output();

	int p[2];
	if (pipe2(p, O_CLOEXEC) != 0) return false;
	pid_t child = fork();
	if (child < 0) {
		close(p[0]);
		close(p[1]);
		return false;
	}
	if (child == 0) {
		setsid(); // own process group, so a kill reaches its children too
		// Ignored signals and the signal mask survive exec: give the program clean defaults.
		signal(SIGPIPE, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, nullptr);
		dup2(p[1], STDOUT_FILENO);
		dup2(p[1], STDERR_FILENO);
		int devnull = open("/dev/null", O_RDONLY | O_CLOEXEC);
		if (devnull >= 0) dup2(devnull, STDIN_FILENO);
		close_range(3, ~0U, CLOSE_RANGE_CLOEXEC); // don't leak our window descriptors
		if (!cwd.empty() && chdir(cwd.c_str()) != 0) child_write("cd ", cwd.c_str(), " failed\n");
		execvpe(a[0], a.data(), e.data());
		child_write("cannot run ", a[0], ": ");
		child_write(strerror(errno), "\n");
		_exit(127);
	}
	close(p[1]);
	os->out_fd = p[0];
	fcntl(os->out_fd, F_SETFL, O_NONBLOCK);
	started(child);
	return true;
}

void Proc::kill_now()
{
	if (!running()) return;
	kill(-(pid_t)pid, SIGKILL);
	kill((pid_t)pid, SIGKILL);
	waitpid((pid_t)pid, nullptr, 0);
	pid = -1;
	exit_status = kKilledExitStatus;
	read_output();
	add_line("---- killed ----");
}

void Proc::terminate()
{
	if (!running()) return;
	kill(-(pid_t)pid, SIGTERM);
	add_line("---- asked to quit (SIGTERM) ----");
}

void Proc::close_output()
{
	if (!os || os->out_fd < 0) return;
	close(os->out_fd);
	os->out_fd = -1;
	if (!partial.empty()) {
		add_line(partial);
		partial.clear();
	}
}

void Proc::read_output()
{
	if (!os || os->out_fd < 0) return;
	char buf[4096];
	ssize_t n;
	while ((n = read(os->out_fd, buf, sizeof buf)) > 0) add_output(buf, n);
	if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) close_output(); // EOF: every writer is gone
}

void Proc::poll()
{
	read_output();
	if (running()) {
		int st;
		if (waitpid((pid_t)pid, &st, WNOHANG) == (pid_t)pid) {
			exit_status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
			kill(-(pid_t)pid, SIGKILL); // leftovers in the group
			pid = -1;
			read_output(); // whatever it wrote just before exiting
			add_line("---- exited (" + std::to_string(exit_status) + ") ----");
		}
	}
}

// ------------------------------------------------------------ /proc scan ---

bool is_shell_wrapper(const std::string &cmd)
{
	return cmd.rfind("/bin/sh -c ", 0) == 0 || cmd.rfind("sh -c ", 0) == 0;
}

bool signal_process(long long pid, bool force) { return ::kill((pid_t)pid, force ? SIGKILL : SIGTERM) == 0; }

static std::string proc_cmdline(const fs::path &dir)
{
	std::ifstream f(dir / "cmdline", std::ios::binary);
	std::string cmd((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	std::replace(cmd.begin(), cmd.end(), '\0', ' ');
	return trim(cmd);
}

void ProcessTable::refresh()
{
	procs_.clear();
	std::error_code ec;
	pid_t self = getpid();
	for (auto &d : fs::directory_iterator("/proc", ec)) {
		std::string n = d.path().filename();
		if (n.empty() || !isdigit((unsigned char)n[0])) continue;
		pid_t p = atoi(n.c_str());
		if (p == self) continue;
		std::string cmd = proc_cmdline(d.path());
		if (cmd.empty() || is_shell_wrapper(cmd)) continue;
		procs_.emplace_back(p, std::move(cmd));
	}
}
