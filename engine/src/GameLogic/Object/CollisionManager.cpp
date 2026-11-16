// OpenBFME. GPL-3.0.
// See GameLogic/Object/CollisionManager.h (fork lane COLLIDE-1: RotWK 2.01 RW 0xB6C420 .. 0xB6E6B0; the Open-BFME-2 decomp's collisionmanager library).

#include "GameLogic/Object/CollisionManager.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/SimMath.h"

#include <string>

#include <algorithm>

CollisionManager::CollisionManager() = default;

CollisionManager::~CollisionManager()
{
	reset();
}

void CollisionManager::reset()
{
	for (Contact *&head : m_buckets)
	{
		while (head)
		{
			Contact *n = head->next;
			delete head;
			head = n;
		}
	}
	m_contactByKey.clear();
	m_candidates.clear();
	for (Record *r : m_records)
	{
		if (r->source)
		{
			r->source->collisionRecord = nullptr;
		}
		delete r;
	}
	m_records.clear();
	m_recordCount = 0;
	m_dirty = nullptr;
	m_axisHeads[0] = m_axisHeads[1] = m_axisHeads[2] = nullptr;
	m_index = 0;
	m_cursor = nullptr;
	m_rebuild = false;
	m_busy = false;
}

// ---- the dirty list (+ 8; head insertion) -------------------------------------------------------------------------------------------------------------
void CollisionManager::pushDirty(Record &r)
{
	if (r.dirty)
	{
		return;
	}
	r.dirty = true;
	r.dirtyPrev = nullptr;
	r.dirtyNext = m_dirty;
	if (m_dirty)
	{
		m_dirty->dirtyPrev = &r;
	}
	m_dirty = &r;
}

void CollisionManager::dropDirty(Record &r)
{
	if (!r.dirty)
	{
		return;
	}
	if (r.dirtyPrev)
	{
		r.dirtyPrev->dirtyNext = r.dirtyNext;
	}
	else
	{
		m_dirty = r.dirtyNext;
	}
	if (r.dirtyNext)
	{
		r.dirtyNext->dirtyPrev = r.dirtyPrev;
	}
	r.dirty = false;
	r.dirtyNext = r.dirtyPrev = nullptr;
}

// RW 0xB6CB80: a collidable that has no record gets one (RW 0xB6C5F0: on the head of the dirty list)
void CollisionManager::registerObject(Collidable &c)
{
	if (c.collisionRecord)
	{
		return;
	}
	Record *r = new Record();
	r->source = &c;
	for (unsigned i = 0; i < 6; ++i)
	{
		r->ep[i].record = r;
		r->ep[i].kind = i ^ 1u; // 1, 0, 3, 2, 5, 4 (RW 0xB6C61D ..): the end of axis i >> 1 at even i
	}
	r->id = c.collisionId();       // + 0x9C: slot 4
	r->group = c.collisionGroup(); // + 0xA0: slot 5
	m_records.push_back(r);
	++m_recordCount;
	pushDirty(*r);
	c.collisionRecord = r;
}

// RW 0xB6D560
void CollisionManager::unregisterObject(Collidable &c)
{
	Record *r = c.collisionRecord;
	if (!r)
	{
		return;
	}
	c.collisionRecord = nullptr;
	if (m_busy)
	{
		// during the update: the record waits on the dirty list without its collidable (the next sort deletes it)
		pushDirty(*r);
		r->source = nullptr;
		return;
	}
	unlinkChain(*r);
	destroyRecord(r);
}

// RW 0xB6C700
void CollisionManager::markDirty(Collidable &c)
{
	if (c.collisionRecord)
	{
		pushDirty(*c.collisionRecord);
	}
}

// the record's destructor (RW 0xB6C740): out of the record list, the axis lists (when in them: ep 0 has a predecessor), the dirty list
void CollisionManager::destroyRecord(Record *r)
{
	if (r->ep[0].prev)
	{
		for (unsigned i = 0; i < 6; ++i)
		{
			unlinkEndpoint(r->ep[i], i >> 1);
		}
	}
	dropDirty(*r);
	auto it = std::find(m_records.begin(), m_records.end(), r);
	if (it != m_records.end())
	{
		m_records.erase(it);
	}
	--m_recordCount;
	if (r->source)
	{
		r->source->collisionRecord = nullptr;
	}
	delete r;
}

// ---- the axis lists -----------------------------------------------------------------------------------------------------------------------------------
void CollisionManager::unlinkEndpoint(Endpoint &e, unsigned axis)
{
	if (e.prev)
	{
		e.prev->next = e.next;
	}
	else if (m_axisHeads[axis] == &e)
	{
		m_axisHeads[axis] = e.next;
	}
	if (e.next)
	{
		e.next->prev = e.prev;
	}
	e.prev = e.next = nullptr;
}

void CollisionManager::pushFront(Endpoint &e, unsigned axis)
{
	e.prev = nullptr;
	e.next = m_axisHeads[axis];
	if (e.next)
	{
		e.next->prev = &e;
	}
	m_axisHeads[axis] = &e;
}

void CollisionManager::openPush(Record *&head, Record *r)
{
	r->openPrev = nullptr;
	r->openNext = head;
	if (head)
	{
		head->openPrev = r;
	}
	head = r;
	r->open = true;
}

void CollisionManager::openRemove(Record *&head, Record *r)
{
	if (!r->open)
	{
		return;
	}
	if (r->openPrev)
	{
		r->openPrev->openNext = r->openNext;
	}
	else
	{
		head = r->openNext;
	}
	if (r->openNext)
	{
		r->openNext->openPrev = r->openPrev;
	}
	r->openNext = r->openPrev = nullptr;
	r->open = false;
}

// insertSorted (inlined in RW 0xB6D8A0): the start after every endpoint not above it (the intervals open there overlap the record), then the end before
// the first endpoint not below it (every start passed overlaps)
void CollisionManager::insertSorted(Record &r, unsigned axis)
{
	Endpoint &start = r.ep[axis * 2 + 1];
	Endpoint &end = r.ep[axis * 2];
	Endpoint *cur = m_axisHeads[axis];
	Endpoint *prev = nullptr;
	Record *open = nullptr;
	const float startValue = start.value;
	while (cur && !(cur->value > startValue))
	{
		if (!cur->isEnd())
		{
			openPush(open, cur->record);
		}
		else
		{
			openRemove(open, cur->record);
		}
		prev = cur;
		cur = cur->next;
	}
	start.prev = prev;
	start.next = cur;
	if (prev)
	{
		prev->next = &start;
	}
	else
	{
		m_axisHeads[axis] = &start;
	}
	if (cur)
	{
		cur->prev = &start;
	}
	prev = &start;
	if (cur)
	{
		while (open)
		{
			Record *o = open;
			addOverlap(o, &r);
			openRemove(open, o);
		}
		const float endValue = end.value;
		do
		{
			if (!(cur->value < endValue))
			{
				break;
			}
			if (!cur->isEnd())
			{
				addOverlap(cur->record, &r);
			}
			prev = cur;
			cur = cur->next;
		} while (cur);
	}
	while (open) // the walk reached the list's end: retail leaves them linked to its local head (no effect)
	{
		openRemove(open, open);
	}
	end.prev = prev;
	end.next = cur;
	prev->next = &end;
	if (cur)
	{
		cur->prev = &end;
	}
}

// RW 0xB6C970 (BFME2 0x7586F0): a bottom-up merge sort of the axis list. The list is dealt alternately onto two lists (each by head insertion), then runs of
// 1, 2, 4, ... are merged, the right run's endpoint taken only when the left's is greater, the merged runs dealt alternately onto the next pair of lists
void CollisionManager::mergeSort(unsigned axis)
{
	struct SortList
	{
		Endpoint *head = nullptr;
		Endpoint *tail = nullptr;
		unsigned count = 0;
	};
	SortList lists[4];
	int i = 0;
	for (Endpoint *cur = m_axisHeads[axis]; cur;)
	{
		Endpoint *next = cur->next;
		cur->next = lists[i].head;
		lists[i].head = cur;
		++lists[i].count;
		i ^= 1;
		cur = next;
	}
	int src = 0;
	unsigned runLen = 1;
	while (lists[src + 1].count != 0)
	{
		SortList *left = &lists[src];
		SortList *right = &lists[src + 1];
		int out = src ^ 2;
		lists[out + 1].count = 0;
		lists[out].count = 0;
		while (left->count != 0)
		{
			unsigned rightRemaining = runLen, leftRemaining = runLen;
			SortList *o = &lists[out];
			for (;;)
			{
				SortList *from;
				if (leftRemaining != 0 && left->count != 0)
				{
					if (rightRemaining != 0 && right->count != 0 && left->head->value > right->head->value)
					{
						from = right;
						--rightRemaining;
					}
					else
					{
						from = left;
						--leftRemaining;
					}
				}
				else if (rightRemaining != 0 && right->count != 0)
				{
					from = right;
					--rightRemaining;
				}
				else
				{
					break;
				}
				--from->count;
				Endpoint *e = from->head;
				from->head = e->next;
				if (o->count == 0)
				{
					o->head = e;
					o->tail = e;
				}
				else
				{
					o->tail->next = e;
					o->tail = e;
				}
				++o->count;
			}
			out ^= 1;
		}
		runLen <<= 1;
		src ^= 2;
	}
	if (lists[src].count > 1)
	{
		lists[src].tail->next = nullptr;
	}
	m_axisHeads[axis] = lists[src].head;
	if (!m_axisHeads[axis])
	{
		return;
	}
	m_axisHeads[axis]->prev = nullptr;
	for (Endpoint *e = m_axisHeads[axis]; e->next; e = e->next)
	{
		e->next->prev = e;
	}
}

// the rebuild of RW 0xB6D8A0: every candidate and contact freed, the lists sorted again, each axis swept (a start overlaps every open interval, the most
// recently opened first)
void CollisionManager::rebuild()
{
	m_rebuild = false;
	++m_rebuilds;
	for (Contact *&head : m_buckets)
	{
		while (head)
		{
			Contact *n = head->next;
			delete head;
			head = n;
		}
	}
	m_contactByKey.clear();
	m_candidates.clear();
	for (Record *r : m_records)
	{
		r->pairs.clear(); // + 0x20 = 0
	}
	for (unsigned k = 0; k < m_axisCount; ++k)
	{
		mergeSort(k);
	}
	for (unsigned k = 0; k < m_axisCount; ++k)
	{
		Record *open = nullptr;
		for (Endpoint *cur = m_axisHeads[k]; cur; cur = cur->next)
		{
			if (!cur->isEnd())
			{
				for (Record *other = open; other; other = other->openNext)
				{
					addOverlap(other, cur->record);
				}
				openPush(open, cur->record);
			}
			else
			{
				openRemove(open, cur->record);
			}
		}
		while (open)
		{
			openRemove(open, open);
		}
	}
}

// ---- the pairs (RW 0xB6D710 and its pair) -------------------------------------------------------------------------------------------------------------
void CollisionManager::addOverlap(Record *a, Record *b)
{
	if (a->group != 0 && a->group == b->group)
	{
		return;
	}
	if (a->id > b->id)
	{
		std::swap(a, b);
	}
	const std::uint64_t k = key(a->id, b->id);
	auto it = m_candidates.find(k);
	if (it == m_candidates.end())
	{
		Candidate c;
		c.first = a;
		c.second = b;
		c.idA = a->id;
		c.idB = b->id;
		c.counter = 1;
		m_candidates.emplace(k, c);
		a->pairs.push_back(k);
		b->pairs.push_back(k);
		return;
	}
	if (++it->second.counter == m_axisCount)
	{
		insertContact(a, b, a->id, b->id);
	}
}

void CollisionManager::removeOverlap(Record *a, Record *b)
{
	if (a->group != 0 && a->group == b->group)
	{
		return;
	}
	if (a->id > b->id)
	{
		std::swap(a, b);
	}
	const std::uint64_t k = key(a->id, b->id);
	auto it = m_candidates.find(k);
	if (it == m_candidates.end())
	{
		return;
	}
	if (it->second.counter == m_axisCount)
	{
		removeContact(a->id, b->id);
	}
	if (--it->second.counter != 0)
	{
		return;
	}
	auto drop = [k](Record *r) {
		auto p = std::find(r->pairs.begin(), r->pairs.end(), k);
		if (p != r->pairs.end())
		{
			r->pairs.erase(p);
		}
	};
	drop(it->second.first);
	drop(it->second.second);
	m_candidates.erase(it);
}

// RW 0xB6D4A0: every candidate of the record goes (from both records' lists, its contact from the contact table)
void CollisionManager::unlinkChain(Record &r)
{
	while (!r.pairs.empty())
	{
		const std::uint64_t k = r.pairs.back();
		auto it = m_candidates.find(k);
		if (it == m_candidates.end())
		{
			r.pairs.pop_back();
			continue;
		}
		Record *other = it->second.first == &r ? it->second.second : it->second.first;
		auto p = std::find(other->pairs.begin(), other->pairs.end(), k);
		if (p != other->pairs.end())
		{
			other->pairs.erase(p);
		}
		r.pairs.pop_back();
		removeContact(it->second.idA, it->second.idB);
		m_candidates.erase(it);
	}
}

// ---- the contacts (the 0x493 buckets at + 0xAE10; head insertion) -------------------------------------------------------------------------------------
void CollisionManager::insertContact(Record *a, Record *b, std::uint32_t idA, std::uint32_t idB)
{
	Contact *c = new Contact();
	c->first = a;
	c->second = b;
	c->idA = idA;
	c->idB = idB;
	c->state = 0;
	c->bucket = (unsigned)(((idA << 16) + idB) % kContactBuckets); // u32
	Contact *&head = m_buckets[c->bucket];
	c->prev = nullptr;
	c->next = head;
	if (head)
	{
		head->prev = c;
	}
	head = c;
	m_contactByKey[key(idA, idB)] = c;
}

void CollisionManager::unlinkContact(Contact *c)
{
	if (m_cursor == c)
	{
		m_cursor = c->next;
	}
	if (c->prev)
	{
		c->prev->next = c->next;
	}
	else
	{
		m_buckets[c->bucket] = c->next;
	}
	if (c->next)
	{
		c->next->prev = c->prev;
	}
	m_contactByKey.erase(key(c->idA, c->idB));
	delete c;
}

void CollisionManager::removeContact(std::uint32_t idA, std::uint32_t idB)
{
	auto it = m_contactByKey.find(key(idA, idB));
	if (it != m_contactByKey.end())
	{
		unlinkContact(it->second);
	}
}

size_t CollisionManager::contactCount() const
{
	return m_contactByKey.size();
}

bool CollisionManager::hasContact(std::uint32_t a, std::uint32_t b) const
{
	if (a > b)
	{
		std::swap(a, b);
	}
	return m_contactByKey.count(key(a, b)) != 0;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> CollisionManager::contactOrder() const
{
	std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
	for (const Contact *head : m_buckets)
	{
		for (const Contact *c = head; c; c = c->next)
		{
			out.emplace_back(c->idA, c->idB);
		}
	}
	return out;
}

// ---- the update ---------------------------------------------------------------------------------------------------------------------------------------
// RW 0xB6C7D0
unsigned CollisionManager::updateCoord(Record &r, bool reinsert)
{
	if (!r.source)
	{
		return 1;
	}
	const std::uint32_t group = r.source->collisionGroup();
	if (group != r.group)
	{
		r.group = group;
	}
	else if (r.ep[0].prev && reinsert)
	{
		return 0;
	}
	Coord3D lo, hi;
	r.source->collisionMinCorner(lo); // slot 0
	r.source->collisionMaxCorner(hi); // slot 1
	if (lo.x >= hi.x || lo.y >= hi.y || lo.z >= hi.z) // x87 fcomp: an unordered compare counts as below (C0 set)
	{
		hi.x = SimMath::pc24Add(lo.x, 1.0f); // RW 0xBD1908
		hi.y = SimMath::pc24Add(lo.y, 1.0f);
		hi.z = SimMath::pc24Add(lo.z, 1.0f);
	}
	auto fabs87 = [](float v) { return v < 0.0f ? -v : v; }; // fabs
	const float dx = fabs87(SimMath::pc24Sub(r.ep[0].value, hi.x));
	const float dy = fabs87(SimMath::pc24Sub(r.ep[2].value, hi.y));
	const float d = dx > dy ? dx : dy;
	unsigned score = 1;
	if (!(d <= 50.0f)) // fcom RW 0xBD88C4, test ah 0x41
	{
		const std::uint32_t t = SimMath::ftol2Low32(SimMath::pc24MulW((double)SimMath::pc24Sub(d, 50.0f), (double)-0.01f)); // RW 0xBDD74C, _ftol2
		score = 11u - t;
		if (score > 10000u)
		{
			score = 10000u;
		}
	}
	r.ep[0].value = hi.x;
	r.ep[1].value = lo.x;
	r.ep[2].value = hi.y;
	r.ep[3].value = lo.y;
	r.ep[4].value = hi.z;
	r.ep[5].value = lo.z;
	return score;
}

// RW 0xB6CF90: the contacts with a dirty record
void CollisionManager::prune()
{
	m_index = 0;
	m_cursor = m_buckets[0];
	for (;;)
	{
		while (!m_cursor)
		{
			if (++m_index == kContactBuckets)
			{
				return;
			}
			m_cursor = m_buckets[m_index];
		}
		Contact *c = m_cursor;
		m_cursor = c->next;
		if (!c->first->dirty && !c->second->dirty)
		{
			continue;
		}
		if (!c->first->source || !c->second->source)
		{
			unlinkContact(c);
		}
		else
		{
			c->state = 0;
		}
	}
}

// RW 0xB6D8A0
void CollisionManager::sort()
{
	std::uint32_t totalScore = 0;
	Record *node = m_dirty;
	while (node)
	{
		const unsigned score = updateCoord(*node, true);
		if (score == 0)
		{
			node = node->dirtyNext;
			continue;
		}
		totalScore += score;
		unlinkChain(*node);
		Record *next = node->dirtyNext;
		if (node->source)
		{
			if (node->ep[0].prev)
			{
				for (unsigned i = 0; i < 6; ++i)
				{
					unlinkEndpoint(node->ep[i], i >> 1);
				}
			}
			for (unsigned k = 0; k < 3; ++k)
			{
				if (k < m_axisCount && !m_rebuild)
				{
					insertSorted(*node, k);
				}
				else
				{
					pushFront(node->ep[k * 2 + 1], k);
					pushFront(node->ep[k * 2], k);
				}
			}
			dropDirty(*node);
		}
		else
		{
			destroyRecord(node);
		}
		node = next;
	}
	while (m_dirty)
	{
		totalScore += updateCoord(*m_dirty, false);
		dropDirty(*m_dirty);
	}
	if (m_records.empty())
	{
		return;
	}
	if (totalScore * 100u <= (std::uint32_t)m_recordCount * 100u && !m_rebuild) // u32, as retail
	{
		for (unsigned k = 0; k < m_axisCount; ++k)
		{
			Endpoint *&head = m_axisHeads[k];
			if (!head)
			{
				continue;
			}
			Endpoint *cur = head->next;
			while (cur)
			{
				if (cur->prev->value <= cur->value)
				{
					cur = cur->next;
					continue;
				}
				Endpoint *pos = cur;
				while (pos->prev && pos->prev->value > cur->value)
				{
					Endpoint *before = pos->prev;
					if (before->isEnd() && !cur->isEnd())
					{
						addOverlap(before->record, cur->record);
					}
					else if (!before->isEnd() && cur->isEnd())
					{
						removeOverlap(before->record, cur->record);
					}
					pos = pos->prev;
				}
				Endpoint *next = cur->next;
				unlinkEndpoint(*cur, k);
				cur->prev = pos->prev;
				cur->next = pos;
				pos->prev = cur;
				if (cur->prev)
				{
					cur->prev->next = cur;
				}
				else
				{
					head = cur;
				}
				cur = next;
			}
		}
		return;
	}
	rebuild();
}

// RW 0xB6D060
void CollisionManager::walk()
{
	m_index = 0;
	m_cursor = m_buckets[0];
	for (;;)
	{
		while (!m_cursor)
		{
			if (++m_index == kContactBuckets)
			{
				return;
			}
			m_cursor = m_buckets[m_index];
		}
		Contact *c = m_cursor;
		m_cursor = c->next;
		if (!c->first->source || !c->second->source)
		{
			continue;
		}
		if (c->state == 0)
		{
			++m_tests;
			c->state = c->first->source->collisionTest(*c->second->source) ? 1 : 2; // slot 8
		}
		if (c->state == 2)
		{
			continue;
		}
		++m_responses;
		c->first->source->collisionRespond(*c->second->source); // slot 9
		if (!c->first->source || !c->second->source)
		{
			continue;
		}
		c->second->source->collisionRespond(*c->first->source); // the normal reversed (no normal is computed)
	}
}

// RW 0xB6E6B0
void CollisionManager::update()
{
	if (m_records.empty())
	{
		return;
	}
	m_busy = true;
	prune();
	sort();
	walk();
	m_busy = false;
}

// ---- state ----------------------------------------------------------------------------------------------------------------------------------------------
void CollisionManager::crc(StateHasher &h) const
{
	h.addU32(0xC0110000u | (std::uint32_t)(m_recordCount & 0xFFFFu));
	std::uint32_t dirty = 0;
	for (const Record *r = m_dirty; r; r = r->dirtyNext)
	{
		++dirty;
	}
	h.addU32(dirty);
	h.addU32((std::uint32_t)m_contactByKey.size());
	for (const Contact *head : m_buckets)
	{
		for (const Contact *c = head; c; c = c->next)
		{
			h.addU32(c->idA);
			h.addU32(c->idB);
			h.addI32(c->state);
		}
	}
}

void CollisionManager::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	const bool loading = x.isLoading();
	Saved s;
	if (!loading)
	{
		for (const Record *r : m_records)
		{
			Saved::Rec rec;
			rec.id = r->id;
			rec.group = r->group;
			for (unsigned i = 0; i < 6; ++i)
			{
				rec.values[i] = r->ep[i].value;
			}
			rec.inLists = r->ep[0].prev != nullptr;
			s.records.push_back(rec);
		}
		for (unsigned k = 0; k < 3; ++k)
		{
			for (const Endpoint *e = m_axisHeads[k]; e; e = e->next)
			{
				const unsigned index = (unsigned)(e - &e->record->ep[0]);
				s.axes[k].emplace_back(e->record->id, (std::uint8_t)index);
			}
		}
		for (const auto &c : m_candidates)
		{
			s.candidates.push_back(c.second);
		}
		std::sort(s.candidates.begin(), s.candidates.end(), [](const Candidate &a, const Candidate &b) { return a.idA != b.idA ? a.idA < b.idA : a.idB < b.idB; });
		s.buckets.resize(kContactBuckets);
		for (unsigned b = 0; b < kContactBuckets; ++b)
		{
			for (const Contact *c = m_buckets[b]; c; c = c->next)
			{
				s.buckets[b].push_back({ { c->idA, c->idB }, c->state });
			}
		}
		for (const Record *r = m_dirty; r; r = r->dirtyNext)
		{
			s.dirty.push_back(r->id);
		}
		s.rebuild = m_rebuild;
	}
	std::uint32_t n = (std::uint32_t)s.records.size();
	x.xferUnsignedInt(&n);
	s.records.resize(n);
	for (Saved::Rec &rec : s.records)
	{
		x.xferUnsignedInt(&rec.id);
		x.xferUnsignedInt(&rec.group);
		for (float &f : rec.values)
		{
			x.xferReal(&f);
		}
		x.xferBool(&rec.inLists);
	}
	for (unsigned k = 0; k < 3; ++k)
	{
		std::uint32_t count = (std::uint32_t)s.axes[k].size();
		x.xferUnsignedInt(&count);
		s.axes[k].resize(count);
		for (auto &e : s.axes[k])
		{
			x.xferUnsignedInt(&e.first);
			x.xferUnsignedByte(&e.second);
			if (e.second > 5)
			{
				throw XferException("CollisionManager: an endpoint index above 5");
			}
		}
	}
	std::uint32_t cands = (std::uint32_t)s.candidates.size();
	x.xferUnsignedInt(&cands);
	s.candidates.resize(cands);
	for (Candidate &c : s.candidates)
	{
		x.xferUnsignedInt(&c.idA);
		x.xferUnsignedInt(&c.idB);
		x.xferUnsignedInt(&c.counter);
	}
	std::uint32_t used = 0;
	for (const auto &b : s.buckets)
	{
		used += b.empty() ? 0u : 1u;
	}
	x.xferUnsignedInt(&used);
	if (loading)
	{
		s.buckets.assign(kContactBuckets, {});
		for (std::uint32_t i = 0; i < used; ++i)
		{
			std::uint32_t bucket = 0, len = 0;
			x.xferUnsignedInt(&bucket);
			x.xferUnsignedInt(&len);
			if (bucket >= kContactBuckets)
			{
				throw XferException("CollisionManager: a contact bucket above 0x492");
			}
			s.buckets[bucket].resize(len);
			for (auto &c : s.buckets[bucket])
			{
				x.xferUnsignedInt(&c.first.first);
				x.xferUnsignedInt(&c.first.second);
				std::int32_t state = 0;
				x.xferInt(&state);
				c.second = state;
			}
		}
	}
	else
	{
		for (std::uint32_t b = 0; b < kContactBuckets; ++b)
		{
			if (s.buckets[b].empty())
			{
				continue;
			}
			std::uint32_t bucket = b, len = (std::uint32_t)s.buckets[b].size();
			x.xferUnsignedInt(&bucket);
			x.xferUnsignedInt(&len);
			for (auto &c : s.buckets[b])
			{
				x.xferUnsignedInt(&c.first.first);
				x.xferUnsignedInt(&c.first.second);
				std::int32_t state = c.second;
				x.xferInt(&state);
			}
		}
	}
	std::uint32_t dirty = (std::uint32_t)s.dirty.size();
	x.xferUnsignedInt(&dirty);
	s.dirty.resize(dirty);
	for (std::uint32_t &id : s.dirty)
	{
		x.xferUnsignedInt(&id);
	}
	x.xferBool(&s.rebuild);
	if (loading)
	{
		s.loaded = true;
		m_saved = std::move(s);
	}
}

void CollisionManager::restore(const std::function<Collidable *(std::uint32_t id)> &find)
{
	if (!m_saved.loaded)
	{
		return;
	}
	Saved s = std::move(m_saved);
	m_saved = Saved{};
	reset();
	std::unordered_map<std::uint32_t, Record *> byId;
	for (const Saved::Rec &rec : s.records)
	{
		Collidable *c = find(rec.id);
		if (!c)
		{
			throw XferException("CollisionManager: the saved record " + std::to_string(rec.id) + " has no object");
		}
		Record *r = new Record();
		r->source = c;
		r->id = rec.id;
		r->group = rec.group;
		for (unsigned i = 0; i < 6; ++i)
		{
			r->ep[i].record = r;
			r->ep[i].kind = i ^ 1u;
			r->ep[i].value = rec.values[i];
		}
		c->collisionRecord = r;
		m_records.push_back(r);
		byId[rec.id] = r;
	}
	m_recordCount = m_records.size();
	auto rec = [&](std::uint32_t id) {
		auto it = byId.find(id);
		if (it == byId.end())
		{
			throw XferException("CollisionManager: a saved reference to the unknown record " + std::to_string(id));
		}
		return it->second;
	};
	for (unsigned k = 0; k < 3; ++k)
	{
		Endpoint *prev = nullptr;
		for (const auto &e : s.axes[k])
		{
			Endpoint *ep = &rec(e.first)->ep[e.second];
			ep->prev = prev;
			ep->next = nullptr;
			if (prev)
			{
				prev->next = ep;
			}
			else
			{
				m_axisHeads[k] = ep;
			}
			prev = ep;
		}
	}
	for (const Candidate &saved : s.candidates)
	{
		Candidate c = saved;
		c.first = rec(c.idA);
		c.second = rec(c.idB);
		const std::uint64_t k = key(c.idA, c.idB);
		m_candidates.emplace(k, c);
		c.first->pairs.push_back(k);
		c.second->pairs.push_back(k);
	}
	for (unsigned b = 0; b < kContactBuckets; ++b)
	{
		Contact *tail = nullptr;
		for (const auto &saved : s.buckets[b])
		{
			Contact *c = new Contact();
			c->idA = saved.first.first;
			c->idB = saved.first.second;
			c->first = rec(c->idA);
			c->second = rec(c->idB);
			c->state = saved.second;
			c->bucket = b;
			c->prev = tail;
			if (tail)
			{
				tail->next = c;
			}
			else
			{
				m_buckets[b] = c;
			}
			tail = c;
			m_contactByKey[key(c->idA, c->idB)] = c;
		}
	}
	for (auto it = s.dirty.rbegin(); it != s.dirty.rend(); ++it)
	{
		pushDirty(*rec(*it));
	}
	m_rebuild = s.rebuild;
}
