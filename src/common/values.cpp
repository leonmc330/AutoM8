#include "values.hpp"
#include "util.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

const char *const kKindIds[] = {"number", "text", "bool"};
const char *const kKindLabels[] = {"number", "text", "yes/no"};
const int kKindCount = 3;

const char *const kOpIds[] = {"+", "-", "*", "/", "and", "or", "xor", "not"};
const char *const kOpLabels[] = {"+  add / join texts", "-  subtract", "*  multiply", "/  divide",
                                 "and", "or", "xor  (one or the other)", "not  (left only)"};
const int kOpCount = sizeof(kOpIds) / sizeof(kOpIds[0]);

const char *const kCmpIds[] = {"<", ">", "<=", ">=", "==", "!="};
const int kCmpCount = sizeof(kCmpIds) / sizeof(kCmpIds[0]);

static const char *kind_name(const Value &v) { return kKindLabels[v.index()]; }

std::string to_text(const Value &v)
{
	switch (kind_of(v)) {
	case VK::Number: {
		char buf[32];
		snprintf(buf, sizeof buf, "%.15g", std::get<double>(v)); // 0.1 + 0.2 shows as 0.3
		return buf;
	}
	case VK::Text: return std::get<std::string>(v);
	case VK::Bool: return std::get<bool>(v) ? "true" : "false";
	}
	return "";
}

bool parse_number(const std::string &s, double &out)
{
	std::string t = trim(s);
	if (t.empty()) return false;
	char *end = nullptr;
	double d = std::strtod(t.c_str(), &end);
	if (*end || !std::isfinite(d)) return false; // "inf" / "nan" are names, not numbers
	out = d;
	return true;
}

static bool is_quoted(const std::string &t) { return !t.empty() && (t[0] == '"' || t[0] == '\''); }

bool is_literal(const std::string &operand)
{
	std::string t = trim(operand);
	double d;
	return is_quoted(t) || t == "true" || t == "false" || parse_number(t, d);
}

bool evaluate(const std::string &operand, const Values &vals, Value &out, std::string &error)
{
	std::string t = trim(operand);
	if (t.empty()) {
		error = "an empty field (write a name, a number, \"text\", true or false)";
		return false;
	}
	if (is_quoted(t)) {
		char q = t[0];
		std::string s;
		for (size_t i = 1; i < t.size(); i++) {
			char c = t[i];
			if (c == '\\' && i + 1 < t.size()) { // \" \' \\ \n
				c = t[++i];
				s += c == 'n' ? '\n' : c;
			} else if (c == q) {
				if (i + 1 != t.size()) break; // text after the closing quote
				out = s;
				return true;
			} else {
				s += c;
			}
		}
		error = "bad quoted text " + t;
		return false;
	}
	if (t == "true" || t == "false") {
		out = t == "true";
		return true;
	}
	double d;
	if (parse_number(t, d)) {
		out = d;
		return true;
	}
	if (!expand_name(t, vals, t, error)) return false;
	auto it = vals.find(t);
	if (it == vals.end()) {
		error = "no value named '" + t + "' (set it first, or put quotes around a text)";
		return false;
	}
	out = it->second;
	return true;
}

bool operate(Op op, const Value &a, const Value &b, Value &out, std::string &error)
{
	auto mismatch = [&](const char *what) {
		error = std::string("'") + kOpIds[(int)op] + "' needs " + what + ", got " + kind_name(a) +
		        (op == Op::Not ? "" : std::string(" and ") + kind_name(b));
		return false;
	};
	switch (op) {
	case Op::Add:
		if (kind_of(a) == VK::Text || kind_of(b) == VK::Text) {
			out = to_text(a) + to_text(b);
			return true;
		}
		[[fallthrough]];
	case Op::Sub:
	case Op::Mul:
	case Op::Div: {
		if (kind_of(a) != VK::Number || kind_of(b) != VK::Number)
			return mismatch(op == Op::Add ? "two numbers or a text" : "two numbers");
		double x = std::get<double>(a), y = std::get<double>(b);
		if (op == Op::Div && y == 0) {
			error = "division by zero";
			return false;
		}
		out = op == Op::Add ? x + y : op == Op::Sub ? x - y : op == Op::Mul ? x * y : x / y;
		return true;
	}
	case Op::And:
	case Op::Or:
	case Op::Xor: {
		if (kind_of(a) != VK::Bool || kind_of(b) != VK::Bool) return mismatch("two yes/no");
		bool x = std::get<bool>(a), y = std::get<bool>(b);
		out = op == Op::And ? x && y : op == Op::Or ? x || y : x != y;
		return true;
	}
	case Op::Not:
		if (kind_of(a) != VK::Bool) return mismatch("a yes/no");
		out = !std::get<bool>(a);
		return true;
	}
	return false;
}

bool compare(Cmp cmp, const Value &a, const Value &b, bool &out, std::string &error)
{
	bool ordering = cmp != Cmp::Eq && cmp != Cmp::Ne;
	if (kind_of(a) != kind_of(b) || (ordering && kind_of(a) == VK::Bool)) {
		if (ordering) {
			error = std::string("can't use '") + kCmpIds[(int)cmp] + "' on " + kind_name(a) + " and " + kind_name(b);
			return false;
		}
		out = cmp == Cmp::Ne && kind_of(a) != kind_of(b); // 1 == "1" is false, like Python
		return true;
	}
	int c = a < b ? -1 : b < a ? 1 : 0; // same alternative: compares the numbers / texts / bools
	switch (cmp) {
	case Cmp::Lt: out = c < 0; break;
	case Cmp::Gt: out = c > 0; break;
	case Cmp::Le: out = c <= 0; break;
	case Cmp::Ge: out = c >= 0; break;
	case Cmp::Eq: out = c == 0; break;
	case Cmp::Ne: out = c != 0; break;
	}
	return true;
}

MaybeKind literal_kind(const std::string &operand)
{
	Value v;
	std::string err;
	if (!is_literal(operand) || !evaluate(operand, {}, v, err)) return std::nullopt;
	return kind_of(v);
}

static bool may_be(MaybeKind k, VK want) { return !k || *k == want; }

bool op_allowed(Op op, MaybeKind l, MaybeKind r)
{
	switch (op) {
	case Op::Add: // numbers, or a text and anything
		if (!l || !r) return true;
		return *l == VK::Text || *r == VK::Text || (*l == VK::Number && *r == VK::Number);
	case Op::Sub:
	case Op::Mul:
	case Op::Div: return may_be(l, VK::Number) && may_be(r, VK::Number);
	case Op::And:
	case Op::Or:
	case Op::Xor: return may_be(l, VK::Bool) && may_be(r, VK::Bool);
	case Op::Not: return may_be(l, VK::Bool);
	}
	return false;
}

MaybeKind op_result(Op op, MaybeKind l, MaybeKind r)
{
	switch (op) {
	case Op::Add:
		if (l == VK::Text || r == VK::Text) return VK::Text;
		if (l == VK::Number && r == VK::Number) return VK::Number;
		return std::nullopt;
	case Op::Sub:
	case Op::Mul:
	case Op::Div: return VK::Number;
	default: return VK::Bool;
	}
}

bool cmp_allowed(Cmp cmp, MaybeKind l, MaybeKind r)
{
	if (cmp == Cmp::Eq || cmp == Cmp::Ne) return true;
	if (l == VK::Bool || r == VK::Bool) return false;
	return !l || !r || *l == *r;
}

// The '}' that closes the '{' at `open` (braces inside are counted), or npos.
static size_t closing_brace(const std::string &s, size_t open)
{
	int depth = 0;
	for (size_t j = open; j < s.size(); j++) {
		if (s[j] == '{') depth++;
		else if (s[j] == '}' && --depth == 0) return j;
	}
	return std::string::npos;
}

std::string substitute(const std::string &text, const Values &vals)
{
	if (vals.empty()) return text;
	std::string out;
	size_t i = 0;
	while (i < text.size()) {
		size_t close;
		if (text[i] != '{' || (close = closing_brace(text, i)) == std::string::npos) {
			out += text[i++];
			continue;
		}
		// {result_{i}}: the inner braces first, then the name they make
		auto it = vals.find(trim(substitute(text.substr(i + 1, close - i - 1), vals)));
		if (it != vals.end()) {
			out += to_text(it->second);
			i = close + 1;
		} else {
			out += text[i++]; // not a value: kept as typed
		}
	}
	return out;
}

bool expand_name(const std::string &name, const Values &vals, std::string &out, std::string &error)
{
	std::string res;
	for (size_t i = 0; i < name.size();) {
		if (name[i] == '}') {
			error = "'" + name + "': a '}' without its '{'";
			return false;
		}
		if (name[i] != '{') {
			res += name[i++];
			continue;
		}
		size_t close = closing_brace(name, i);
		if (close == std::string::npos) {
			error = "'" + name + "': a '{' without its '}'";
			return false;
		}
		std::string inner;
		if (!expand_name(trim(name.substr(i + 1, close - i - 1)), vals, inner, error)) return false;
		auto it = vals.find(inner);
		if (it == vals.end()) {
			error = "'" + name + "': no value named '" + inner + "'";
			return false;
		}
		res += to_text(it->second);
		i = close + 1;
	}
	out = trim(res);
	return true;
}
