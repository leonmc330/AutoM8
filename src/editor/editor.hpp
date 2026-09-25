#pragma once

#include "blocks.hpp"

#include <string>

struct Editor {
	std::string path;
	std::string path_input; // the file field in the top bar
	Document doc;
	int selected = 0;       // selected button
	std::string status;

	explicit Editor(const std::string &path);
	void draw();
	void save();
};
