// process.hpp on Windows. A program runs in its own job object, the equivalent of a
// process group: killing the job kills everything the program started.
#include "process.hpp"
#include "util.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cwctype>
#include <map>

struct Proc::Os {
	HANDLE out = nullptr;     // read end of the stdout+stderr pipe
	HANDLE process = nullptr;
	HANDLE job = nullptr;
};

static void close_handle(HANDLE &h)
{
	if (h) CloseHandle(h);
	h = nullptr;
}

static void release(Proc::Os *os)
{
	close_handle(os->process);
	close_handle(os->job);
}

Proc::~Proc()
{
	close_output();
	if (os) release(os);
	delete os;
}

static std::string last_error_text()
{
	DWORD err = GetLastError();
	wchar_t *msg = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
	               err, 0, (wchar_t *)&msg, 0, nullptr);
	std::string s = msg ? trim(narrow(msg)) : "error " + std::to_string(err);
	LocalFree(msg);
	return s;
}

// Quotes one argument so that CommandLineToArgvW / the C runtime give it back unchanged.
static std::wstring quote_arg(const std::wstring &a)
{
	if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
	std::wstring out = L"\"";
	for (size_t i = 0;; i++) {
		size_t backslashes = 0;
		while (i < a.size() && a[i] == L'\\') {
			backslashes++;
			i++;
		}
		if (i == a.size()) {
			out.append(backslashes * 2, L'\\');
			break;
		}
		if (a[i] == L'"') {
			out.append(backslashes * 2 + 1, L'\\');
		} else {
			out.append(backslashes, L'\\');
		}
		out += a[i];
	}
	return out + L"\"";
}

static std::wstring comspec()
{
	const wchar_t *c = _wgetenv(L"ComSpec");
	return c && *c ? c : L"cmd.exe";
}

// Environment names are case-insensitive, and CreateProcess wants the block sorted.
struct NoCaseLess {
	bool operator()(const std::wstring &a, const std::wstring &b) const { return _wcsicmp(a.c_str(), b.c_str()) < 0; }
};

static std::wstring environment_block(const std::map<std::string, std::string> &env_over)
{
	std::map<std::wstring, std::wstring, NoCaseLess> env;
	if (wchar_t *block = GetEnvironmentStringsW()) {
		for (wchar_t *e = block; *e; e += wcslen(e) + 1) {
			const wchar_t *eq = wcschr(e + 1, L'='); // +1: names like "=C:" start with '='
			if (eq) env[std::wstring(e, eq - e)] = eq + 1;
		}
		FreeEnvironmentStringsW(block);
	}
	for (auto &kv : env_over) env[widen(kv.first)] = widen(kv.second);
	std::wstring out;
	for (auto &kv : env) {
		out += kv.first + L"=" + kv.second;
		out += L'\0';
	}
	out += L'\0';
	return out;
}

bool Proc::spawn(const std::vector<std::string> &argv, const std::string &shell_command,
                 const std::map<std::string, std::string> &env_over, const std::string &cwd)
{
	if (!os) os = new Os;
	read_output(); // output still buffered from the previous run belongs to it
	close_output();
	release(os);

	std::wstring cmdline;
	if (shell_command.empty() && !argv.empty()) {
		for (auto &a : argv) cmdline += (cmdline.empty() ? L"" : L" ") + quote_arg(widen(a));
	} else { // cmd /s /c "..." runs everything between the outer quotes as typed
		cmdline = quote_arg(comspec()) + L" /d /s /c \"" + widen(shell_command) + L"\"";
	}
	std::wstring env = environment_block(env_over);
	std::wstring wcwd = widen(cwd);

	SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
	HANDLE rd = nullptr, wr = nullptr;
	if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
	SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

	// Only these handles are inherited, not every inheritable handle AutoM8 has open.
	HANDLE inherit[2] = {wr, nul};
	SIZE_T list_size = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
	std::vector<char> list_buf(list_size);
	auto *list = (LPPROC_THREAD_ATTRIBUTE_LIST)list_buf.data();
	bool list_ok = InitializeProcThreadAttributeList(list, 1, 0, &list_size);
	bool have_list = list_ok && UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
	                                                      (nul != INVALID_HANDLE_VALUE ? 2 : 1) * sizeof(HANDLE),
	                                                      nullptr, nullptr);

	STARTUPINFOEXW si = {};
	si.StartupInfo.cb = have_list ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW);
	si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	si.StartupInfo.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
	si.StartupInfo.hStdOutput = wr;
	si.StartupInfo.hStdError = wr;
	si.lpAttributeList = have_list ? list : nullptr;

	// Suspended until it is in its job, so its first children land in the job too.
	// CREATE_NO_WINDOW: console programs get no console window (their output is captured).
	DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW | CREATE_SUSPENDED |
	              (have_list ? EXTENDED_STARTUPINFO_PRESENT : 0);
	PROCESS_INFORMATION pi = {};
	BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE, flags, env.data(),
	                         wcwd.empty() ? nullptr : wcwd.c_str(), &si.StartupInfo, &pi);
	std::string error = ok ? "" : last_error_text();
	if (list_ok) DeleteProcThreadAttributeList(list);
	CloseHandle(wr);
	if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
	if (!ok) {
		CloseHandle(rd);
		add_line("---- cannot run " + (argv.empty() ? shell_command : argv[0]) + ": " + error + " ----");
		return false;
	}

	os->job = CreateJobObjectW(nullptr, nullptr);
	if (os->job && !AssignProcessToJobObject(os->job, pi.hProcess)) close_handle(os->job); // then: no tree kill
	ResumeThread(pi.hThread);
	CloseHandle(pi.hThread);
	os->out = rd;
	os->process = pi.hProcess;
	started(pi.dwProcessId);
	return true;
}

// ------------------------------------------------------- asking to quit ---

struct CloseWindows {
	DWORD pid;   // close this process's windows...
	HANDLE job;  // ... or, if set, the windows of every process in this job
	int closed = 0;
};

static BOOL CALLBACK close_window(HWND hwnd, LPARAM param)
{
	auto *cw = (CloseWindows *)param;
	DWORD wpid = 0;
	GetWindowThreadProcessId(hwnd, &wpid);
	bool ours = wpid == cw->pid;
	if (!ours && cw->job) {
		HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, wpid);
		BOOL in = FALSE;
		if (h && IsProcessInJob(h, cw->job, &in) && in) ours = true;
		if (h) CloseHandle(h);
	}
	if (ours && PostMessageW(hwnd, WM_CLOSE, 0, 0)) cw->closed++;
	return TRUE;
}

static int close_windows(DWORD pid, HANDLE job)
{
	CloseWindows cw = {pid, job};
	EnumWindows(close_window, (LPARAM)&cw);
	return cw.closed;
}

void Proc::kill_now()
{
	if (!running()) return;
	if (os->job) TerminateJobObject(os->job, kKilledExitStatus);
	else TerminateProcess(os->process, kKilledExitStatus);
	WaitForSingleObject(os->process, 5000);
	release(os);
	pid = -1;
	exit_status = kKilledExitStatus;
	read_output();
	add_line("---- killed ----");
}

void Proc::terminate()
{
	if (!running()) return;
	int n = close_windows((DWORD)pid, os->job);
	add_line(n ? "---- asked to quit (closed its windows) ----"
	           : "---- asked to quit, but it has no window to close ----");
}

void Proc::close_output()
{
	if (!os || !os->out) return;
	close_handle(os->out);
	if (!partial.empty()) {
		add_line(partial);
		partial.clear();
	}
}

void Proc::read_output()
{
	if (!os || !os->out) return;
	char buf[4096];
	for (;;) {
		DWORD avail = 0, got = 0;
		if (!PeekNamedPipe(os->out, nullptr, 0, nullptr, &avail, nullptr)) break; // broken pipe: every writer is gone
		if (avail == 0) return;
		if (!ReadFile(os->out, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr) || got == 0) break;
		add_output(buf, got);
	}
	close_output();
}

void Proc::poll()
{
	read_output();
	if (running() && WaitForSingleObject(os->process, 0) == WAIT_OBJECT_0) {
		DWORD code = 0;
		GetExitCodeProcess(os->process, &code);
		exit_status = code > (DWORD)INT_MAX ? INT_MAX : (int)code; // NTSTATUS crash codes are huge
		if (os->job) TerminateJobObject(os->job, kKilledExitStatus); // leftovers, like the process group on Linux
		release(os);
		pid = -1;
		read_output(); // whatever it wrote just before exiting
		char hex[16];
		snprintf(hex, sizeof hex, "0x%08lX", (unsigned long)code);
		add_line("---- exited (" + (code > 255 ? std::string(hex) : std::to_string(code)) + ") ----");
	}
}

// ------------------------------------------------------- process list ---

bool is_shell_wrapper(const std::string &cmd)
{
	std::string lower = cmd;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
	return lower.find("cmd.exe") != std::string::npos && lower.find(" /d /s /c ") != std::string::npos;
}

bool signal_process(long long pid, bool force)
{
	if (!force) return close_windows((DWORD)pid, nullptr) > 0;
	HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
	if (!h) return false;
	BOOL ok = TerminateProcess(h, kKilledExitStatus);
	CloseHandle(h);
	return ok;
}

// The full command line of another process (Windows 8.1+), or "" if we may not read it.
static std::string command_line(DWORD pid)
{
	using NtQueryInformationProcessFn = LONG(WINAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);
	static auto query = (NtQueryInformationProcessFn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
	                                                                        "NtQueryInformationProcess");
	struct UnicodeString {
		USHORT Length, MaximumLength;
		PWSTR Buffer;
	};
	constexpr ULONG kProcessCommandLineInformation = 60;
	if (!query) return "";
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!h) return "";
	std::string out;
	ULONG size = 0;
	query(h, kProcessCommandLineInformation, nullptr, 0, &size);
	if (size >= sizeof(UnicodeString)) {
		std::vector<char> buf(size);
		if (query(h, kProcessCommandLineInformation, buf.data(), size, &size) >= 0) {
			auto *us = (UnicodeString *)buf.data();
			out = narrow(std::wstring(us->Buffer, us->Length / sizeof(wchar_t)));
		}
	}
	CloseHandle(h);
	return trim(out);
}

void ProcessTable::refresh()
{
	procs_.clear();
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	DWORD self = GetCurrentProcessId();
	PROCESSENTRY32W pe = {};
	pe.dwSize = sizeof pe;
	for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe)) {
		if (pe.th32ProcessID == self || pe.th32ProcessID <= 4) continue; // us, Idle, System
		std::string cmd = command_line(pe.th32ProcessID);
		if (cmd.empty()) cmd = narrow(pe.szExeFile); // not ours to read: match on the exe name
		if (cmd.empty() || is_shell_wrapper(cmd)) continue;
		procs_.emplace_back(pe.th32ProcessID, std::move(cmd));
	}
	CloseHandle(snap);
}
