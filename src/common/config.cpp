#include "config.hpp"
#include "util.hpp"

#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string config_path() { return path_string(path_of(config_dir()) / "config.json"); }

static json read_json(const std::string &path)
{
	std::ifstream f(path_of(path));
	if (!f) return json::object();
	json j = json::parse(f, nullptr, false); // no exceptions: a broken file counts as empty
	return j.is_object() ? j : json::object();
}

Config load_config(const std::string &path)
{
	json j = read_json(path);
	Config c;
	if (j.contains("ui_scale") && j["ui_scale"].is_number()) c.ui_scale = j["ui_scale"].get<float>();
	if (j.contains("last_file") && j["last_file"].is_string()) c.last_file = j["last_file"].get<std::string>();
	return c;
}

// Reads the file, sets one key, writes it back (keys this version doesn't know are kept).
static bool update(const std::string &path, const char *key, const json &value)
{
	json j = read_json(path);
	if (j.contains("version") && (!j["version"].is_number_integer() || j["version"].get<int>() > kConfigVersion))
		return false;
	j["version"] = kConfigVersion;
	j[key] = value;

	std::error_code ec;
	fs::path target = path_of(path);
	fs::create_directories(target.parent_path(), ec);
	fs::path tmp = path_of(path + ".tmp"); // write then rename: the other program never reads half a file
	{
		std::ofstream f(tmp);
		f << j.dump(2) << "\n";
		f.close();
		if (!f) {
			fs::remove(tmp, ec);
			return false;
		}
	}
	fs::rename(tmp, target, ec);
	if (ec) fs::remove(tmp, ec);
	return !ec;
}

bool save_ui_scale(float scale, const std::string &path) { return update(path, "ui_scale", scale); }
bool save_last_file(const std::string &file, const std::string &path) { return update(path, "last_file", file); }
