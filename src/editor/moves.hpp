// Moving / deleting blocks in a Document. Places are index paths, not pointers:
// a pointer into a std::vector dies when that vector (or a parent) is modified,
// and a drag & drop spans several frames.
#pragma once

#include "blocks.hpp"

// Which Seq: a button's sequence, then down through nested blocks.
struct SeqPath {
	static constexpr int kMaxDepth = 32;
	struct Step {
		int idx;    // block in the current Seq
		bool other; // false = body ("then" / "do"), true = else_body
	};
	int button = -1;
	int depth = 0;
	Step steps[kMaxDepth] = {};

	bool can_descend() const { return depth < kMaxDepth; }
	SeqPath child(int idx, bool other) const; // requires can_descend()
};

Seq *resolve(Document &doc, const SeqPath &p); // nullptr if the path no longer exists

struct Move {
	SeqPath from;
	int from_idx = -1;
	bool remove = false; // true: delete the block, `to` unused
	SeqPath to;
	int to_idx = -1;     // insert before this index (the size of the Seq = append)
};

bool apply_move(Document &doc, const Move &m); // true if the document changed
