// Perl-compatible regular expressions (PCRE2). Unlike std::regex it does not recurse
// per character, so long program output can't overflow the stack, and it is much faster.
#pragma once

#include <string>

class Regex {
public:
	explicit Regex(const std::string &pattern);
	~Regex();
	Regex(const Regex &) = delete;
	Regex &operator=(const Regex &) = delete;

	bool ok() const { return code_ != nullptr; }
	const std::string &error() const { return error_; }

	// 1 = found, 0 = not found, -1 = gave up (pattern too expensive for this text)
	int search(const std::string &text) const;

private:
	void *code_ = nullptr; // pcre2_code_8 *
	std::string error_;
};
