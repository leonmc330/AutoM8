#include "regex.hpp"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

Regex::Regex(const std::string &pattern)
{
	int err;
	PCRE2_SIZE offset;
	code_ = pcre2_compile((PCRE2_SPTR)pattern.c_str(), pattern.size(), 0, &err, &offset, nullptr);
	if (!code_) {
		PCRE2_UCHAR msg[256];
		pcre2_get_error_message(err, msg, sizeof msg);
		error_ = std::string((const char *)msg) + " at offset " + std::to_string(offset);
	}
}

Regex::~Regex() { pcre2_code_free((pcre2_code *)code_); }

int Regex::search(const std::string &text) const
{
	if (!code_) return 0;
	auto *code = (pcre2_code *)code_;
	pcre2_match_data *md = pcre2_match_data_create_from_pattern(code, nullptr);
	if (!md) return -1;
	int rc = pcre2_match(code, (PCRE2_SPTR)text.data(), text.size(), 0, 0, md, nullptr);
	pcre2_match_data_free(md);
	if (rc >= 0) return 1;
	if (rc == PCRE2_ERROR_NOMATCH) return 0;
	return -1; // match / heap limit reached
}
