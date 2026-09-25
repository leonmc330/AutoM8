#include "util.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
std::wstring widen(const std::string &s)
{
	if (s.empty()) return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
	return w;
}

std::string narrow(const std::wstring &w)
{
	if (w.empty()) return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
	return s;
}

static std::string env_or(const char *name, const std::string &fallback)
{
	std::string v;
	return get_env(name, v) && !v.empty() ? v : fallback;
}

std::string home() { return env_or("USERPROFILE", "C:\\"); }

std::string exe_dir()
{
	std::wstring buf(MAX_PATH, L'\0');
	for (;;) {
		DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
		if (n == 0) return path_string(fs::current_path());
		if (n < buf.size()) {
			buf.resize(n);
			break;
		}
		buf.resize(buf.size() * 2); // long path
	}
	return path_string(fs::path(buf).parent_path());
}

std::string config_dir() { return path_string(path_of(env_or("APPDATA", home())) / "autom8"); }
#else
std::string home()
{
	const char *h = getenv("HOME");
	return h ? h : "/tmp";
}

std::string exe_dir()
{
	std::error_code ec;
	fs::path p = fs::read_symlink("/proc/self/exe", ec);
	return ec ? fs::current_path().string() : p.parent_path().string();
}

std::string config_dir()
{
	const char *x = getenv("XDG_CONFIG_HOME");
	return (x && x[0] == '/' ? std::string(x) : home() + "/.config") + "/autom8";
}
#endif

std::vector<std::string> utf8_args(int argc, char **argv)
{
#ifdef _WIN32
	(void)argc;
	(void)argv;
	std::vector<std::string> out;
	int n = 0;
	if (wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n)) {
		for (int i = 0; i < n; i++) out.push_back(narrow(w[i]));
		LocalFree(w);
	}
	return out;
#else
	return std::vector<std::string>(argv, argv + argc);
#endif
}

std::filesystem::path path_of(const std::string &utf8)
{
#ifdef _WIN32
	return fs::path(widen(utf8));
#else
	return fs::path(utf8);
#endif
}

std::string path_string(const std::filesystem::path &p)
{
#ifdef _WIN32
	return narrow(p.wstring());
#else
	return p.string();
#endif
}

bool get_env(const std::string &name, std::string &value)
{
#ifdef _WIN32
	const wchar_t *v = _wgetenv(widen(name).c_str());
	if (!v) return false;
	value = narrow(v);
#else
	const char *v = getenv(name.c_str());
	if (!v) return false;
	value = v;
#endif
	return true;
}

std::string document_path(const std::string &given)
{
	if (!given.empty()) return path_string(fs::absolute(path_of(expand(given))));
	// A sequences.json next to the program wins (portable install); else the user config folder.
	fs::path portable = path_of(exe_dir()) / "sequences.json";
	std::error_code ec;
	if (fs::exists(portable, ec)) return path_string(portable);
	return path_string(path_of(config_dir()) / "sequences.json");
}

std::string with_json_extension(const std::string &path)
{
	if (path.empty() || path_of(path).has_extension()) return path;
	return path + ".json";
}

std::string trim(std::string s)
{
	while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
	size_t i = 0;
	while (i < s.size() && isspace((unsigned char)s[i])) i++;
	return s.substr(i);
}

std::vector<std::string> split_list(const std::string &s, char sep)
{
	std::vector<std::string> out;
	std::string cur;
	for (char ch : s + sep) {
		if (ch == sep) {
			std::string t = trim(cur);
			if (!t.empty()) out.push_back(t);
			cur.clear();
		} else {
			cur += ch;
		}
	}
	return out;
}

std::string expand(const std::string &in)
{
	std::string s = in;
	if (s == "~" || s.rfind("~/", 0) == 0) s = home() + s.substr(1);
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] != '$' || i + 1 >= s.size()) { out += s[i]; continue; }
		size_t start = i + 1, end;
		std::string name;
		if (s[start] == '{') {
			end = s.find('}', start);
			if (end == std::string::npos) { out += s[i]; continue; }
			name = s.substr(start + 1, end - start - 1);
			i = end;
		} else {
			end = start;
			while (end < s.size() && (isalnum((unsigned char)s[end]) || s[end] == '_')) end++;
			if (end == start) { out += s[i]; continue; }
			name = s.substr(start, end - start);
			i = end - 1;
		}
		std::string v;
		if (get_env(name, v)) out += v;
	}
	return out;
}

std::vector<std::string> split_command(const std::string &s)
{
	std::vector<std::string> out;
	std::string cur;
	bool have = false; // an empty "" still counts as an argument
	char q = 0;
	for (char ch : s) {
		if (q) {
			if (ch == q) q = 0;
			else cur += ch;
		} else if (ch == '"' || ch == '\'') {
			q = ch;
			have = true;
		} else if (isspace((unsigned char)ch)) {
			if (have || !cur.empty()) out.push_back(cur);
			cur.clear();
			have = false;
		} else {
			cur += ch;
		}
	}
	if (have || !cur.empty()) out.push_back(cur);
	return out;
}
