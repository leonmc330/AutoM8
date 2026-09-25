#include "moves.hpp"

#include <algorithm>

SeqPath SeqPath::child(int idx, bool other) const
{
	SeqPath p = *this;
	p.steps[p.depth++] = {idx, other};
	return p;
}

Seq *resolve(Document &doc, const SeqPath &p)
{
	if (p.button < 0 || p.button >= (int)doc.buttons.size()) return nullptr;
	Seq *s = &doc.buttons[p.button].seq;
	for (int i = 0; i < p.depth; i++) {
		const SeqPath::Step &st = p.steps[i];
		if (st.idx < 0 || st.idx >= (int)s->size()) return nullptr;
		Block &b = (*s)[st.idx];
		s = st.other ? &b.else_body : &b.body;
	}
	return s;
}

// Does `p` go through the first `n` steps of `prefix`?
static bool starts_with(const SeqPath &p, const SeqPath &prefix, int n)
{
	if (p.button != prefix.button || p.depth < n) return false;
	for (int i = 0; i < n; i++)
		if (p.steps[i].idx != prefix.steps[i].idx || p.steps[i].other != prefix.steps[i].other) return false;
	return true;
}

bool apply_move(Document &doc, const Move &m)
{
	Seq *from = resolve(doc, m.from);
	if (!from || m.from_idx < 0 || m.from_idx >= (int)from->size()) return false;
	if (m.remove) {
		from->erase(from->begin() + m.from_idx);
		return true;
	}

	SeqPath to = m.to;
	int to_idx = m.to_idx;
	bool to_below_from = starts_with(to, m.from, m.from.depth); // `to` is `from` or inside one of its blocks
	if (to_below_from && to.depth > m.from.depth && to.steps[m.from.depth].idx == m.from_idx)
		return false; // can't move a block into itself
	if (!resolve(doc, to)) return false;

	Block b = std::move((*from)[m.from_idx]);
	from->erase(from->begin() + m.from_idx);

	// The erase shifted the blocks after it one place up: fix the target path.
	if (to_below_from) {
		if (to.depth == m.from.depth) {
			if (to_idx > m.from_idx) to_idx--;
		} else if (to.steps[m.from.depth].idx > m.from_idx) {
			to.steps[m.from.depth].idx--;
		}
	}
	Seq *dst = resolve(doc, to); // resolved again: the old pointer may be stale now
	if (!dst) { // can't happen, but never lose the block
		from->insert(from->begin() + m.from_idx, std::move(b));
		return false;
	}
	to_idx = std::clamp(to_idx, 0, (int)dst->size());
	dst->insert(dst->begin() + to_idx, std::move(b));
	return true;
}
