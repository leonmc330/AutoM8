// Unit tests for the parts that don't need a window: `make test`.

#include "blocks.hpp"
#include "config.hpp"
#include "moves.hpp"
#include "process.hpp"
#include "regex.hpp"
#include "runner.hpp"
#include "values.hpp"
#include "util.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#include <algorithm>

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
	CHECK(with_json_extension("dir/setup") == "dir/setup.json");
	CHECK(with_json_extension("dir/setup.json") == "dir/setup.json");
	CHECK(with_json_extension("dir/setup.txt") == "dir/setup.txt");
	CHECK(with_json_extension("") == "");
}

static void test_block_table()
{
	// Every block type is listed once, and each family's blocks are together ("+ add block" submenus).
	std::set<int> types;
	std::set<std::string> families_done;
	for (int i = 0; i < kBlockCount; i++) {
		types.insert((int)kBlocks[i].type);
		if (i > 0 && std::string(kBlocks[i].family) != kBlocks[i - 1].family) {
			CHECK(!families_done.count(kBlocks[i].family));
			families_done.insert(kBlocks[i - 1].family);
		}
	}
	CHECK((int)types.size() == kBlockCount && kBlockCount == (int)BT::Thread + 1);
}

static void test_json_round_trip()
{
	Document d = default_document();
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
	CHECK(back.buttons[0].seq.back().env == "A=1\nB=2");
	CHECK(back.buttons[0].seq.back().shell);
	CHECK(back.buttons[1].seq[0].type == BT::RepeatN && back.buttons[1].seq[0].body.size() == 2);
	// The editor tells unsaved changes by comparing document_json(): same document, same text.
	CHECK(document_json(back) == document_json(d));
	back.buttons[0].name += "!";
	CHECK(document_json(back) != document_json(d));
	{
		std::ifstream in(path_of(path));
		std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		CHECK(saved == document_json(d));
	}

	// Files from older versions still load: "on_close" is ignored, and is not written back.
	std::ofstream(path_of(path)) << "{ \"buttons\": [ {\"name\": \"A\", \"sequence\": []} ], \"on_close\": \"A\" }";
	CHECK(load_document(path, back, &err));
	CHECK(back.buttons.size() == 1 && back.buttons[0].name == "A");
	CHECK(save_document(path, back));
	{
		std::ifstream in(path_of(path));
		std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		CHECK(saved.find("on_close") == std::string::npos);
	}

	// Every file says which format version it is in; files from before versions are version 1.
	CHECK(document_json(d).find("\"version\": " + std::to_string(kDocumentVersion)) != std::string::npos);
	bool incompatible = true;
	std::ofstream(path_of(path)) << "{ \"buttons\": [] }";
	CHECK(load_document(path, back, &err, &incompatible) && !incompatible);
	// A newer (or too old) format is refused, and said so.
	std::ofstream(path_of(path)) << "{ \"version\": " << kDocumentVersion + 1 << ", \"buttons\": [] }";
	CHECK(!load_document(path, back, &err, &incompatible) && incompatible);
	CHECK(err.find("newer") != std::string::npos);
	std::ofstream(path_of(path)) << "{ \"version\": " << kMinDocumentVersion - 1 << ", \"buttons\": [] }";
	CHECK(!load_document(path, back, &err, &incompatible) && incompatible);
	std::ofstream(path_of(path)) << "{ \"version\": \"1\", \"buttons\": [] }";
	CHECK(!load_document(path, back, &err, &incompatible) && !incompatible);

	// A broken file is reported, not loaded.
	std::ofstream(path_of(path)) << "{ \"buttons\": [ {\"name\": 3} ]";
	CHECK(!load_document(path, back, &err));
	CHECK(!err.empty());
	fs::remove(path_of(path));
}

static void test_config()
{
	std::string path = path_string(fs::temp_directory_path() / "autom8-test-dir" / "config.json");
	fs::remove_all(path_of(path).parent_path());
	Config c = load_config(path); // no file: defaults
	CHECK(c.ui_scale == 0 && c.last_file.empty());
	CHECK(save_ui_scale(1.5f, path)); // creates the folder
	CHECK(save_last_file("/some/file.json", path));
	c = load_config(path);
	CHECK(c.ui_scale == 1.5f && c.last_file == "/some/file.json"); // one setting doesn't drop the other
	CHECK(save_ui_scale(2, path));
	CHECK(load_config(path).last_file == "/some/file.json");

	// A config from a newer version is read, but not written over.
	std::ofstream(path_of(path)) << "{ \"version\": " << kConfigVersion + 1 << ", \"ui_scale\": 3 }";
	CHECK(load_config(path).ui_scale == 3);
	CHECK(!save_ui_scale(1, path));
	CHECK(load_config(path).ui_scale == 3);

	std::ofstream(path_of(path)) << "not json";
	CHECK(load_config(path).ui_scale == 0);
	CHECK(save_ui_scale(1.25f, path)); // a broken file is replaced
	CHECK(load_config(path).ui_scale == 1.25f);
	fs::remove_all(path_of(path).parent_path());
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

static void test_values()
{
	Values vals = {{"n", 3.0}, {"s", std::string("hi")}, {"yes", true}};
	Value v;
	std::string err;
	CHECK(evaluate(" n ", vals, v, err) && std::get<double>(v) == 3);
	CHECK(evaluate("2.5", vals, v, err) && std::get<double>(v) == 2.5);
	CHECK(evaluate("\"a \\\"b\\\"\"", vals, v, err) && std::get<std::string>(v) == "a \"b\"");
	CHECK(evaluate("'x'", vals, v, err) && std::get<std::string>(v) == "x");
	CHECK(evaluate("false", vals, v, err) && !std::get<bool>(v));
	CHECK(!evaluate("nope", vals, v, err) && err.find("nope") != std::string::npos);
	CHECK(!evaluate("\"open", vals, v, err));
	CHECK(!evaluate("", vals, v, err));
	CHECK(is_literal("1e3") && is_literal("\"t\"") && !is_literal("n") && !is_literal("inf"));

	auto op = [&](Op o, Value a, Value b) {
		Value out;
		return operate(o, a, b, out, err) ? to_text(out) : "error";
	};
	CHECK(op(Op::Add, 1.0, 2.0) == "3");
	CHECK(op(Op::Add, 0.1, 0.2) == "0.3");
	CHECK(op(Op::Sub, 1.0, 2.5) == "-1.5");
	CHECK(op(Op::Mul, 4.0, 2.5) == "10");
	CHECK(op(Op::Div, 1.0, 4.0) == "0.25");
	CHECK(op(Op::Div, 1.0, 0.0) == "error" && err == "division by zero");
	CHECK(op(Op::Add, std::string("string1 "), std::string("string2")) == "string1 string2");
	CHECK(op(Op::Add, std::string("n = "), 3.0) == "n = 3");
	CHECK(op(Op::Sub, std::string("a"), 1.0) == "error");
	CHECK(op(Op::And, true, false) == "false" && op(Op::Or, true, false) == "true");
	CHECK(op(Op::Xor, true, true) == "false" && op(Op::Not, true, false) == "false");
	CHECK(op(Op::And, true, 1.0) == "error" && op(Op::Add, true, 1.0) == "error");

	auto cmp = [&](Cmp c, Value a, Value b) {
		bool out = false;
		return compare(c, a, b, out, err) ? (out ? 1 : 0) : -1;
	};
	CHECK(cmp(Cmp::Lt, 2.0, 10.0) == 1 && cmp(Cmp::Ge, 2.0, 2.0) == 1 && cmp(Cmp::Gt, 2.0, 2.0) == 0);
	CHECK(cmp(Cmp::Lt, std::string("abc"), std::string("abd")) == 1);
	CHECK(cmp(Cmp::Eq, std::string("a"), std::string("a")) == 1 && cmp(Cmp::Ne, true, false) == 1);
	CHECK(cmp(Cmp::Eq, 1.0, std::string("1")) == 0 && cmp(Cmp::Ne, 1.0, std::string("1")) == 1);
	CHECK(cmp(Cmp::Lt, 1.0, std::string("1")) == -1 && cmp(Cmp::Lt, true, false) == -1);

	CHECK(substitute("n={n}, {s}! {none} {yes}{", vals) == "n=3, hi! {none} true{");

	// The editor offers only the operators that work on what it knows of the operands.
	CHECK(literal_kind("2") == VK::Number && literal_kind("'a'") == VK::Text && literal_kind("true") == VK::Bool);
	CHECK(!literal_kind("name") && !literal_kind(""));
	MaybeKind num = VK::Number, text = VK::Text, yn = VK::Bool, unknown;
	CHECK(op_allowed(Op::Add, num, num) && op_allowed(Op::Mul, num, num) && !op_allowed(Op::And, num, num));
	CHECK(op_allowed(Op::Add, text, text) && op_allowed(Op::Add, text, num) && !op_allowed(Op::Sub, text, text));
	CHECK(!op_allowed(Op::And, text, text) && !op_allowed(Op::Not, text, unknown));
	CHECK(op_allowed(Op::Xor, yn, yn) && op_allowed(Op::Not, yn, unknown) && !op_allowed(Op::Add, yn, yn));
	CHECK(!op_allowed(Op::Add, num, yn) && op_allowed(Op::Add, unknown, yn) && op_allowed(Op::Div, unknown, unknown));
	CHECK(op_result(Op::Add, text, num) == VK::Text && op_result(Op::Add, num, num) == VK::Number);
	CHECK(!op_result(Op::Add, num, unknown) && op_result(Op::Or, unknown, unknown) == VK::Bool);
	CHECK(cmp_allowed(Cmp::Lt, num, num) && cmp_allowed(Cmp::Lt, text, text) && !cmp_allowed(Cmp::Lt, yn, yn));
	CHECK(cmp_allowed(Cmp::Eq, yn, yn) && !cmp_allowed(Cmp::Gt, num, text) && cmp_allowed(Cmp::Ne, num, text));
	CHECK(cmp_allowed(Cmp::Ge, unknown, num) && !cmp_allowed(Cmp::Ge, unknown, yn));
	CHECK(substitute("{n}", {}) == "{n}");
}

static Block set_value(const std::string &name, VK kind, const std::string &text, bool flag = false)
{
	Block b;
	b.type = BT::SetValue;
	b.name = name;
	b.kind = kind;
	b.command = text;
	b.flag = flag;
	return b;
}

static Block operate_block(const std::string &name, const std::string &l, Op o, const std::string &r)
{
	Block b;
	b.type = BT::Operate;
	b.name = name;
	b.left = l;
	b.op = o;
	b.right = r;
	return b;
}

static void run_button(Runner &r, const std::string &name)
{
	r.error_text.clear();
	r.press(name);
	for (int i = 0; i < 1000 && r.active; i++) r.update();
}

static void test_value_blocks()
{
	// count to 3 with a Repeat until, then join texts and check yes/no logic
	Block loop;
	loop.type = BT::RepeatUntil;
	loop.cond.type = CT::Compare;
	loop.cond.left = "i";
	loop.cond.cmp = Cmp::Ge;
	loop.cond.right = "3";
	loop.body = {operate_block("i", "i", Op::Add, "1")};
	Block check = block_if({msg("both")});
	check.cond.type = CT::ValueTrue;
	check.cond.left = "both";
	check.else_body = {msg("not both")};

	Document d;
	d.buttons.push_back({"count", {0, 0, 0, 1},
	                     {set_value("i", VK::Number, "0"), loop, set_value("s", VK::Text, "string1 "),
	                      operate_block("s", "s", Op::Add, "\"string2\""), msg("i={i} s={s}"),
	                      set_value("a", VK::Bool, "", true), operate_block("b", "a", Op::Not, ""),
	                      operate_block("both", "a", Op::Or, "b"), check}});
	d.buttons.push_back({"bad number", {0, 0, 0, 1}, {set_value("x", VK::Number, "12abc"), msg("after")}});
	d.buttons.push_back({"unknown", {0, 0, 0, 1}, {block_if({msg("then")})}});
	d.buttons.back().seq[0].cond.type = CT::ValueTrue;
	d.buttons.back().seq[0].cond.left = "missing";
	d.buttons.push_back({"sees nothing", {0, 0, 0, 1}, {msg("i={i}")}});

	std::string path = path_string(fs::temp_directory_path() / "autom8-values-test.json");
	CHECK(save_document(path, d));
	// The file round-trips, typed values included.
	Document back;
	CHECK(load_document(path, back));
	CHECK(document_json(back) == document_json(d));
	CHECK(back.buttons[0].seq[0].kind == VK::Number && back.buttons[0].seq[0].command == "0");
	CHECK(back.buttons[0].seq[1].cond.cmp == Cmp::Ge && back.buttons[0].seq[3].op == Op::Add);
	CHECK(back.buttons[1].seq[0].command == "12abc"); // kept as typed, fails when run
	CHECK(document_json(d).find("\"value\": 0.0") != std::string::npos);

	Runner r(path);
	auto said = [&](const std::string &s) { return std::find(r.history.begin(), r.history.end(), s) != r.history.end(); };
	run_button(r, "count");
	CHECK(r.error_text.empty());
	CHECK(said("i=3 s=string1 string2"));
	CHECK(said("both") && !said("not both"));
	CHECK(r.values.empty()); // dropped once the sequence ended

	run_button(r, "bad number");
	CHECK(r.error_text.find("not a number") != std::string::npos && !said("after"));
	run_button(r, "unknown");
	CHECK(r.error_text.find("missing") != std::string::npos && !said("then"));
	// Values belong to one press of one button.
	run_button(r, "sees nothing");
	CHECK(said("i={i}"));
	fs::remove(path_of(path));
}

static Block thread_block(int count, bool blocking, Seq body)
{
	Block b;
	b.type = BT::Thread;
	b.count = count;
	b.name = "t_index";
	b.blocking = blocking;
	b.body = std::move(body);
	return b;
}

// Runs in real time (waits, programs): gives up after `limit` seconds.
static void run_button_timed(Runner &r, const std::string &name, double limit = 10)
{
	r.error_text.clear();
	r.press(name);
	auto t0 = Clock::now();
	while (r.active && secs_since(t0) < limit) {
		r.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

static void test_threads()
{
	Block wait_long;
	wait_long.type = BT::WaitSeconds;
	wait_long.seconds = 30;
	Block wait_short = wait_long;
	wait_short.seconds = 0.2f;
	Block stop;
	stop.type = BT::Stop;
	Block fail;
	fail.type = BT::Throw;
	fail.command = "thread {t_index} failed";
	Block echo;
	echo.type = BT::Run;
	echo.name = "echo";
	echo.command = "echo hello";
	echo.shell = true;
	echo.blocking = true;
	echo.max_s = 10;
	Block heard = block_if({operate_block("heard", "heard", Op::Add, "1")});
	heard.cond.type = CT::OutputMatches;
	heard.cond.program = "echo";
	heard.cond.text = "hello";
	Block inner = thread_block(2, true, {msg("inner {t_index} {outer}")});
	inner.name = "t_inner";
	inner.body = {msg("inner {outer}.{t_inner}")};
	Block killed = thread_block(3, true, {wait_long, msg("never")});
	killed.max_s = 0.2f;

	Document d;
	// Each thread sees its own index; the others values are shared (the sum is 1+2+...+5).
	d.buttons.push_back({"sum", {0, 0, 0, 1},
	                     {set_value("sum", VK::Number, "0"),
	                      thread_block(5, true, {operate_block("sum", "sum", Op::Add, "t_index"), msg("t{t_index}")}),
	                      msg("sum={sum} index={t_index}")}});
	d.buttons.push_back({"not blocking", {0, 0, 0, 1},
	                     {thread_block(2, false, {wait_short, msg("thread {t_index} ends")}), msg("main goes on")}});
	d.buttons.push_back({"max time", {0, 0, 0, 1}, {killed, msg("after")}});
	d.buttons.push_back({"stop", {0, 0, 0, 1}, {thread_block(2, true, {stop, msg("never")}), msg("after")}});
	d.buttons.push_back({"throw", {0, 0, 0, 1}, {thread_block(2, true, {fail}), msg("never")}});
	d.buttons.push_back({"programs", {0, 0, 0, 1},
	                     {set_value("heard", VK::Number, "0"), thread_block(2, true, {echo, heard}), msg("heard={heard}")}});
	Block outer = thread_block(2, true, {inner});
	outer.name = "outer";
	d.buttons.push_back({"nested", {0, 0, 0, 1}, {outer}});

	std::string path = path_string(fs::temp_directory_path() / "autom8-threads-test.json");
	CHECK(save_document(path, d));
	Document back;
	CHECK(load_document(path, back));
	CHECK(document_json(back) == document_json(d));
	CHECK(back.buttons[2].seq[0].type == BT::Thread && back.buttons[2].seq[0].count == 3 &&
	      back.buttons[2].seq[0].name == "t_index" && back.buttons[2].seq[0].blocking &&
	      back.buttons[2].seq[0].max_s > 0.1f && back.buttons[2].seq[0].body.size() == 2);

	Runner r(path);
	auto at = [&](const std::string &s) {
		auto it = std::find(r.history.begin(), r.history.end(), s);
		return it == r.history.end() ? -1 : (int)(it - r.history.begin());
	};
	run_button(r, "sum");
	CHECK(r.error_text.empty() && !r.active);
	CHECK(at("t1") >= 0 && at("t5") >= 0);
	CHECK(at("sum=15 index={t_index}") > at("t5")); // blocking: after every thread; the index is theirs only

	run_button_timed(r, "not blocking");
	CHECK(!r.active && r.error_text.empty());
	CHECK(at("main goes on") >= 0 && at("main goes on") < at("thread 1 ends") && at("thread 2 ends") >= 0);
	CHECK(at("not blocking: done") > at("thread 2 ends")); // the button ends with its last thread

	auto t0 = Clock::now();
	run_button_timed(r, "max time");
	CHECK(secs_since(t0) < 5 && at("Thread #3: killed after 0.2 s") >= 0 && at("never") < 0 && at("after") >= 0);

	run_button(r, "stop"); // Stop ends the thread only
	CHECK(r.error_text.empty() && at("never") < 0 && at("after") >= 0);
	run_button(r, "throw"); // an error ends the whole sequence
	CHECK(r.error_text == "thread 1 failed" && at("never") < 0);

	run_button_timed(r, "programs"); // each thread runs its own copy of a program
	CHECK(r.error_text.empty() && at("heard=2") >= 0);
	CHECK(r.procs.count("echo #1") && r.procs.count("echo #2") && !r.procs.count("echo"));
	CHECK(!r.program_running("echo"));

	run_button(r, "nested"); // a thread inside a thread keeps the outer index
	CHECK(at("inner 1.1") >= 0 && at("inner 1.2") >= 0 && at("inner 2.1") >= 0 && at("inner 2.2") >= 0);
	fs::remove(path_of(path));
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

// A thread killed after its max time takes its programs with it when asked to.
static void test_thread_timeout_kills()
{
	Block prog;
	prog.type = BT::Run;
	prog.name = "sleeper";
	prog.command = kLongRunning;
	prog.shell = true;
	Block wait_long;
	wait_long.type = BT::WaitSeconds;
	wait_long.seconds = 30;
	Block killing = thread_block(2, true, {prog, wait_long});
	killing.max_s = 0.3f;
	killing.kill_on_timeout = true;
	Block keeping = killing;
	keeping.kill_on_timeout = false;

	Document d;
	d.buttons.push_back({"kill", {0, 0, 0, 1}, {killing}});
	d.buttons.push_back({"keep", {0, 0, 0, 1}, {keeping}});
	std::string path = path_string(fs::temp_directory_path() / "autom8-thread-kill-test.json");
	CHECK(save_document(path, d));
	Document back;
	CHECK(load_document(path, back) && back.buttons[0].seq[0].kill_on_timeout && !back.buttons[1].seq[0].kill_on_timeout);

	Runner r(path);
	run_button_timed(r, "kill");
	CHECK(!r.active && r.procs.count("sleeper #1") && r.procs.count("sleeper #2"));
	CHECK(!r.program_running("sleeper"));
	run_button_timed(r, "keep");
	CHECK(!r.active && r.procs["sleeper #1"].running() && r.procs["sleeper #2"].running());
	r.kill_program("sleeper");
	CHECK(!r.procs["sleeper #1"].running() && !r.procs["sleeper #2"].running());
	fs::remove(path_of(path));
}

int main()
{
	test_util();
	test_block_table();
	test_json_round_trip();
	test_config();
	test_moves();
	test_values();
	test_value_blocks();
	test_threads();
	test_regex();
	test_process();
	test_thread_timeout_kills();
	printf("%d passed, %d failed\n", g_passed, g_failed);
	return g_failed ? 1 : 0;
}
