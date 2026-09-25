// autom8: runs the buttons of a sequence file.

#include "gui.hpp"
#include "runner.hpp"
#include "window.hpp"

#include <algorithm>
#include <filesystem>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace fs = std::filesystem;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

static const char kUsage[] =
	"usage:\n"
	"  autom8 [--sequence FILE]                  open the window\n"
	"  autom8 [--sequence FILE] --list           print the buttons, one per line\n"
	"  autom8 [--sequence FILE] --press NAME     press a button in the terminal, no window\n"
	"  autom8 --version | --help\n"
	"\n"
	"options:\n"
	"  --sequence FILE   the sequence file to use (FILE alone works too)\n"
	"  --list            print the names of the buttons in the file\n"
	"  --press NAME      run that button's sequence and exit when it is done\n"
	"  --verbose         with --press: also print the output of every program\n"
	"  --stay            with --press: keep printing after the sequence, until Ctrl+C\n"
	"\n"
	"exit codes (--list / --press): 0 ok, 1 the sequence threw an error or the file\n"
	"can't be read, 2 wrong arguments / no such button / no such file, 130 Ctrl+C\n"
	"\n"
	"Without --sequence: sequences.json next to this program if it exists,\n"
#ifdef _WIN32
	"else %APPDATA%\\autom8\\sequences.json.\n";
#else
	"else $XDG_CONFIG_HOME/autom8/sequences.json (~/.config/autom8/...).\n";
#endif

// Terminal mode: prints status changes, asks questions on stdin.
static volatile sig_atomic_t g_stop = 0;

#ifdef _WIN32
static BOOL WINAPI on_console_event(DWORD type)
{
	if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
	g_stop = 1;
	return TRUE;
}

static void catch_ctrl_c() { SetConsoleCtrlHandler(on_console_event, TRUE); }

// Started from Explorer, a console program gets a console window of its own: hide it in window mode.
static void hide_own_console()
{
	DWORD pids[2];
	if (GetConsoleProcessList(pids, 2) == 1) ShowWindow(GetConsoleWindow(), SW_HIDE);
}
#else
static void catch_ctrl_c()
{
	struct sigaction sa = {};
	sa.sa_handler = [](int) { g_stop = 1; }; // no SA_RESTART: Ctrl+C also interrupts a question
	sigaction(SIGINT, &sa, nullptr);
}
#endif

static int run_headless(Runner &r, const std::string &button, bool verbose, bool stay)
{
	std::map<std::string, size_t> shown_lines; // per program, lines already printed
	auto print_output = [&] {
		if (!verbose) return;
		for (auto &kv : r.procs) {
			const Proc &p = kv.second;
			size_t &shown = shown_lines[kv.first];
			size_t first = p.total_lines() - p.log.size(); // oldest line still kept
			for (size_t i = std::max(shown, first); i < p.total_lines(); i++)
				printf("  [%s] %s\n", kv.first.c_str(), p.log[i - first].c_str());
			shown = p.total_lines();
		}
	};
	if (!r.doc.find(button)) {
		fprintf(stderr, "autom8: no button named '%s' in %s (see --list)\n", button.c_str(), r.path.c_str());
		return 2;
	}
	catch_ctrl_c();
	r.press(button);
	size_t printed = r.history_start + r.history.size() - 1; // print from the "running" line on
	auto flush_history = [&] {
		printed = std::max(printed, r.history_start);
		for (; printed < r.history_start + r.history.size(); printed++)
			printf("%s\n", r.history[printed - r.history_start].c_str());
		fflush(stdout);
	};
	while (r.active && !g_stop) {
		r.update();
		print_output();
		flush_history();
		if (r.ask_open) {
			r.ask_open = false;
			printf("%s [y/N] ", r.ask_text.c_str());
			fflush(stdout);
			std::string answer;
			if (!std::getline(std::cin, answer)) {
				std::cin.clear();
				printf("\n");
			}
			r.ask_answer = !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
		}
		r.popup_text.clear(); // already printed: every message is in the history too
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
	flush_history();
	if (g_stop) {
		fprintf(stderr, "autom8: interrupted (programs already started keep running)\n");
		return 130;
	}
	if (stay) { // keep the programs' output pipes open (and printed) until Ctrl+C
		printf("(still running, Ctrl+C to quit autom8)\n");
		while (!g_stop) {
			for (auto &kv : r.procs) kv.second.poll();
			print_output();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
	if (!r.error_text.empty()) {
		fprintf(stderr, "error: %s\n", r.error_text.c_str());
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8); // our strings are UTF-8
	SetConsoleCP(CP_UTF8);
#else
	signal(SIGPIPE, SIG_IGN); // children get it back (see Proc::spawn)
#endif
	std::vector<std::string> args = utf8_args(argc, argv);
	std::string file, press;
	bool list = false, verbose = false, stay = false;
	auto usage_error = [](const std::string &msg) {
		fprintf(stderr, "autom8: %s\n\n%s", msg.c_str(), kUsage);
		return 2;
	};
	for (size_t i = 1; i < args.size(); i++) {
		const std::string &a = args[i];
		auto value = [&](std::string &out) {
			if (i + 1 >= args.size()) return false;
			out = args[++i];
			return true;
		};
		if (a == "-h" || a == "--help") {
			fputs(kUsage, stdout);
			return 0;
		} else if (a == "--version") {
			puts("autom8 " AUTOM8_VERSION);
			return 0;
		} else if (a == "--sequence") {
			if (!value(file)) return usage_error("--sequence needs a file");
		} else if (a == "--press" || a == "--run") { // --run: the old name
			if (!value(press)) return usage_error(a + " needs a button name");
		} else if (a == "--list") {
			list = true;
		} else if (a == "--verbose") {
			verbose = true;
		} else if (a == "--stay") {
			stay = true;
		} else if (!a.empty() && a[0] == '-') {
			return usage_error("unknown option " + a);
		} else if (file.empty()) {
			file = a;
		} else {
			return usage_error("one sequence file only (got " + file + " and " + a + ")");
		}
	}
	if (list && !press.empty()) return usage_error("--list and --press can't be used together");
	if ((verbose || stay) && press.empty()) return usage_error("--verbose and --stay only work with --press");

	// Terminal modes never create a file: a typo in the path must be an error, not an empty file.
	bool terminal = list || !press.empty();
	std::error_code ec;
	if (terminal && !file.empty() && !fs::exists(path_of(document_path(file)), ec)) {
		fprintf(stderr, "autom8: no such file: %s\n", file.c_str());
		return 2;
	}

	Runner r(document_path(file));
	if (terminal && !r.load_error.empty()) {
		fprintf(stderr, "autom8: cannot read %s\n", r.load_error.c_str()); // the error names the file
		return 1;
	}
	if (list) {
		for (auto &b : r.doc.buttons) puts(b.name.c_str());
		return 0;
	}
	if (!press.empty()) return run_headless(r, press, verbose, stay);
#ifdef _WIN32
	hide_own_console();
#endif

	GuiCallbacks cb;
	cb.update = [&] {
		r.update();
		r.reload_if_changed();
	};
	cb.draw = [&] { draw_runner_window(r); };
	cb.busy = [&] { return r.active; };
	return run_gui("AutoM8 " AUTOM8_VERSION, 760, 420, cb) ? 0 : 1;
}
