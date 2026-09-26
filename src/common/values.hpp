// Values: variables a sequence sets and tests while it runs (a number, a text or a
// yes/no). They belong to one press of one button and are dropped when it ends.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <variant>

enum class VK { Number, Text, Bool };
extern const char *const kKindIds[];    // "number", "text", "bool" (JSON)
extern const char *const kKindLabels[]; // editor names
extern const int kKindCount;

// Operate block: result = left OP right ("not" only uses left).
enum class Op { Add, Sub, Mul, Div, And, Or, Xor, Not };
extern const char *const kOpIds[];    // "+", "-", ..., "and", "not"
extern const char *const kOpLabels[]; // editor names
extern const int kOpCount;

// Compare condition.
enum class Cmp { Lt, Gt, Le, Ge, Eq, Ne };
extern const char *const kCmpIds[]; // "<", ">", "<=", ">=", "==", "!="
extern const int kCmpCount;

using Value = std::variant<double, std::string, bool>; // index matches VK
using Values = std::map<std::string, Value>;

inline VK kind_of(const Value &v) { return (VK)v.index(); }
std::string to_text(const Value &v); // 3 -> "3", 2.5 -> "2.5", true -> "true"
bool parse_number(const std::string &s, double &out); // the whole text (trimmed) must be a number

// An operand as typed in a field, Python-like: `"text"` or 'text' (a literal text),
// true / false, a number, or else the name of a value.
bool is_literal(const std::string &operand);
// false: `error` says why (unknown name, unfinished quote...).
bool evaluate(const std::string &operand, const Values &vals, Value &out, std::string &error);

// false: `error` says why (mixed types, division by zero...).
// + joins texts; a text + a number or yes/no joins it as text.
bool operate(Op op, const Value &a, const Value &b, Value &out, std::string &error);
// Numbers and texts compare with all six; yes/no only with == and !=;
// different kinds are never equal and can't be ordered.
bool compare(Cmp cmp, const Value &a, const Value &b, bool &out, std::string &error);

// What the editor can tell before running: a kind, or nullopt when it can't know it (a name nothing
// sets, an empty field). An unknown kind allows anything.
using MaybeKind = std::optional<VK>;
MaybeKind literal_kind(const std::string &operand); // nullopt: not a literal
bool op_allowed(Op op, MaybeKind left, MaybeKind right); // "not" ignores `right`
MaybeKind op_result(Op op, MaybeKind left, MaybeKind right);
bool cmp_allowed(Cmp cmp, MaybeKind left, MaybeKind right);

// "Count: {n}" -> "Count: 3" for every {name} that is a value; other braces are kept.
std::string substitute(const std::string &text, const Values &vals);
