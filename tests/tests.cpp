// Unit tests for the parts that don't need a window: `make test`.

#include "blocks.hpp"
#include "moves.hpp"
#include "process.hpp"
#include "regex.hpp"
#include "util.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static int g_failed = 0, g_passed = 0;

#define CHECK(cond)                                                              \
	do {                                                                         \
		if (cond) {                                                              \
			g_passed++;                                                          \
		} else {                                                                 \
			g_failed++;                                                          \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                        \
	} while (0)

static Block msg(const std::string &text)
{
	Block b;
	b.type = BT::Message;
	b.command = text;
	return b;
}

static Block block_if(Seq then_body)
{
	Block b;
	b.type = BT::If;
	b.body = std::move(then_body);
	return b;
}

static void test_util()
{
	CHECK(trim("  a b \n") == "a b");
	CHECK((split_list("a, b,,c ,", ',') == std::vector<std::string>{"a", "b", "c"}));
	CHECK((split_command("run \"a b\" 'c d' \"\" e") == std::vector<std::string>{"run", "a b", "c d", "", "e"}));
#ifdef _WIN32
	_putenv_s("AUTOM8_TEST", "xyz");
#else
	setenv("AUTOM8_TEST", "xyz", 1);
#endif
	CHECK(expand("$AUTOM8_TEST/${AUTOM8_TEST}-$") == "xyz/xyz-$");
	CHECK(expand("~/f") == home() + "/f");
	CHECK(expand("a~b") == "a~b");
}

static void test_json_round_trip()
{
	Document d = default_document();
	d.on_close = "Countdown";
	Block run;
	run.type = BT::Run;
	run.name = "prog";
	run.command = "echo hi";
	run.env = "A=1\nB=2";
	run.shell = true;
	d.buttons[0].seq.push_back(run);

	std::string path = path_string(fs::temp_directory_path() / "autom8-test.json");
	CHECK(save_document(path, d));
	Document back;
	std::string err;
	CHECK(load_document(path, back, &err));
	CHECK(back.buttons.size() == d.buttons.size());
	CHECK(back.on_close == "Countdown");
	CHECK(back.buttons[0].seq.back().env == "A=1\nB=2");
	CHECK(back.buttons[0].seq.back().shell);
	CHECK(back.buttons[1].seq[0].type == BT::RepeatN && back.buttons[1].seq[0].body.size() == 2);

	// A broken file is reported, not loaded.
	std::ofstream(path_of(path)) << "{ \"buttons\": [ {\"name\": 3} ]";
	CHECK(!load_document(path, back, &err));
	CHECK(!err.empty());
	fs::remove(path_of(path));
}

static void test_moves()
{
	// top: [A, If{B}, C]
	Document d;
	d.buttons.push_back({"b", {0, 0, 0, 1}, {msg("A"), block_if({msg("B")}), msg("C")}});
	SeqPath top;
	top.button = 0;
	SeqPath inside_if = top.child(1, false);

	// Drag A into the If's body, which comes after it: this used to write to a destroyed vector.
	CHECK(apply_move(d, {top, 0, false, inside_if, 0}));
	Seq &s = d.buttons[0].seq;
	CHECK(s.size() == 2 && s[0].type == BT::If && s[1].command == "C");
	CHECK(s[0].body.size() == 2 && s[0].body[0].command == "A" && s[0].body[1].command == "B");

	// Drag B out of the If to the end of the top list.
	inside_if = top.child(0, false);
	CHECK(apply_move(d, {inside_if, 1, false, top, 2}));
	CHECK(s.size() == 3 && s[2].command == "B" && s[0].body.size() == 1);

	// A block can't go into itself.
	CHECK(!apply_move(d, {top, 0, false, inside_if, 0}));

	// Move down in the same list ("v" button: to = i + 2).
	CHECK(apply_move(d, {top, 1, false, top, 3}));
	CHECK(s[1].command == "B" && s[2].command == "C");

	// Delete, and stale paths are ignored.
	CHECK(apply_move(d, {top, 2, true, {}, -1}));
	CHECK(s.size() == 2);
	CHECK(!apply_move(d, {top, 7, true, {}, -1}));
	SeqPath gone = top.child(5, false);
	CHECK(!apply_move(d, {top, 0, false, gone, 0}));
	CHECK(s.size() == 2);
}

static void test_regex()
{
	// The default VR check pattern on ~400 KB of output crashed std::regex (stack overflow).
	std::string text = "LHR-AB: Connected to receiver 0123456789\n";
	while (text.size() < 400000) text += std::string(79, 'x') + "\n";
	text += "LHR-CD: Connected to receiver ABCDEF0123\n";
	Regex re("(LHR-[0-9A-F]+: Connected to receiver [0-9A-F]{10}[\\s\\S]*){2}");
	CHECK(re.ok());
	CHECK(re.search(text) == 1);
	CHECK(Regex("receiver 999").search(text) == 0);
	Regex bad("(unclosed");
	CHECK(!bad.ok() && !bad.error().empty());
}

static void wait_exit(Proc &p)
{
	for (int i = 0; i < 500 && p.running(); i++) {
		p.poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	p.poll();
}

// Shell commands that work the same in sh (Linux) and cmd (Windows) where possible.
#ifdef _WIN32
static const char *kEnvAndExit3 = "echo %AUTOM8_X%& exit 3";
static const char *kLongRunning = "ping -n 30 127.0.0.1 >NUL";
static std::vector<std::string> kFindable = {"ping", "-n", "31", "127.0.0.1"};
static const char *kFindablePattern = "-n 31 127.0.0.1";
static const int kDescriptorSlack = 8; // Windows opens a few handles of its own now and then

static int open_descriptors()
{
	DWORD n = 0;
	GetProcessHandleCount(GetCurrentProcess(), &n);
	return (int)n;
}
#else
static const char *kEnvAndExit3 = "echo $AUTOM8_X; exit 3";
static const char *kLongRunning = "sleep 30";
static std::vector<std::string> kFindable = {"sleep", "29.5"};
static const char *kFindablePattern = "sleep 29.5";
static const int kDescriptorSlack = 0;

static int open_descriptors()
{
	return (int)std::distance(fs::directory_iterator("/proc/self/fd"), fs::directory_iterator{});
}
#endif

static void test_process()
{
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN); // like autom8 does
#endif

	Proc p;
	CHECK(p.start_shell("echo out&& echo err 1>&2", {}, ""));
	wait_exit(p);
	CHECK(p.exit_status == 0);
	std::string out = p.current_output();
	CHECK(out.find("out\n") != std::string::npos);
	CHECK(out.find("err") != std::string::npos);
	CHECK(out.find('\r') == std::string::npos); // Windows line endings are stripped

#ifndef _WIN32
	// A last line without a newline is kept.
	CHECK(p.start_shell("printf tail", {}, ""));
	wait_exit(p);
	CHECK(p.current_output().find("tail\n") != std::string::npos);

	// Children must not inherit SIGPIPE ignored (bit 13 of SigIgn).
	CHECK(p.start({"/bin/sh", "-c", "grep SigIgn /proc/self/status"}, {}, ""));
	wait_exit(p);
	out = p.current_output();
	size_t at = out.find("SigIgn:");
	CHECK(at != std::string::npos);
	if (at != std::string::npos) {
		unsigned long long mask = strtoull(out.c_str() + at + 7, nullptr, 16);
		CHECK((mask & (1ULL << (SIGPIPE - 1))) == 0);
	}
#endif

	// Environment overrides, exit codes, and the log only holds the current run.
	CHECK(p.start_shell(kEnvAndExit3, {{"AUTOM8_X", "hello"}}, ""));
	wait_exit(p);
	CHECK(p.exit_status == 3);
	CHECK(p.current_output().find("hello") != std::string::npos);
	CHECK(p.current_output().find("out") == std::string::npos);

	// Clearing the log keeps current_output() pointing at the right lines.
	p.clear_log();
	CHECK(p.current_output().empty());

	// A program that doesn't exist fails cleanly.
	Proc missing;
	missing.start({"autom8-no-such-program"}, {}, "");
	wait_exit(missing);
	CHECK(!missing.running() && missing.exit_status != 0);

	// Restarting many times must not leak descriptors / handles.
	for (int i = 0; i < 3; i++) { // warm up lazily created OS state
		p.start_shell("exit 0", {}, "");
		wait_exit(p);
	}
	int before = open_descriptors();
	for (int i = 0; i < 50; i++) {
		p.start_shell("exit 0", {}, "");
		wait_exit(p);
	}
	CHECK(open_descriptors() <= before + kDescriptorSlack);

	// Killing a long-running program (and, through its group / job, the child the shell started).
	CHECK(p.start_shell(kLongRunning, {}, ""));
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	p.kill_now();
	CHECK(!p.running() && p.exit_status == kKilledExitStatus);

	// Finding and killing processes by command line.
	Proc findable;
	CHECK(findable.start(kFindable, {}, ""));
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	ProcessTable table;
	table.refresh();
	CHECK(table.any({kFindablePattern}));
	CHECK(!table.any({"autom8-pattern-nobody-has"}));
	CHECK(table.kill({kFindablePattern}, true) >= 1);
	wait_exit(findable);
	CHECK(!findable.running());
}

int main()
{
	test_util();
	test_json_round_trip();
	test_moves();
	test_regex();
	test_process();
	printf("%d passed, %d failed\n", g_passed, g_failed);
	return g_failed ? 1 : 0;
}
