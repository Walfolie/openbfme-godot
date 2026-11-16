// OpenBFME. GPL-3.0.
//
// TheCollisionManager (RotWK RW 0xDE4360, subsystem vtable RW 0xD0BF08; fork lane COLLIDE-1): the broad phase and the contact walk every registered object's
// collide modules meet each other through. The library (Code/Libraries/Source/collisionmanager in the Open-BFME-2 decomp, CollisionManagerImpl.cpp) is the
// BFME2 1.06 code at RW = BFME2 VA + 0x414280 (checked: UnRegisterObject BFME2 0x7592E0 = RW 0xB6D560, the contact walk BFME2 0x758DE0 = RW 0xB6D060);
// every rule below was read in the RotWK disassembly as well.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * a registered object (RW 0xB6CB80) gets a record (0xA4 bytes, ctor RW 0xB6C5F0): its id (collidable slot 4) and group (slot 5), six axis endpoints
//     (+ 0x24, 0x14 bytes each: prev, next, record, kind, value; kind bit 0 = the interval's end, kind >> 1 = the axis; the ends hold the max corner, the
//     starts the min corner), the list of its candidate pairs (+ 0x20); it goes on the head of the dirty list and of the record list (+ 4 counts them);
//   * update (RW 0xB6E6B0): only with a record: busy; the prune (RW 0xB6CF90), the sort (RW 0xB6D8A0) and the contact walk (RW 0xB6D060); not busy;
//   * the prune: every contact (the 0x493 buckets in order, each chain from its head) with a dirty record: removed when a record lost its collidable, else
//     tested again (state 0);
//   * the sort: each dirty record whose UpdateCoord(true) (RW 0xB6C7D0) scores (a new record, or a changed group) loses its pairs (RW 0xB6D4A0), leaves the
//     axis lists and is inserted again in each (insertSorted, or both endpoints at the front while a rebuild is due); a record without a collidable is
//     deleted. The other dirty records take their new bounds (UpdateCoord(false)) in place. Then, with a total score * 100 at most the record count * 100
//     (u32) and no rebuild due: an insertion sort per axis, where a start passing an end begins an overlap and an end passing a start ends one; else the
//     rebuild: every pair and contact freed, a bottom-up merge sort (RW 0x7586F0 + 0x414280) and a sweep;
//   * a pair (RW 0xB6D710 AddOverlap / RemoveOverlap): never two records of one non-zero group (two IMMOBILE objects); the lower id first; a candidate
//     ((a << 16) + b) % 0x2B7B counts the axes overlapping, and at 3 (+ 0xC068) the contact goes to the head of bucket ((a << 16) + b) % 0x493 with state 0;
//   * the contact walk: state 0 -> the first's test (slot 8) of the second ? 1 : 2; state 1 -> the first responds (slot 9) to the second, then (both still
//     registered) the second to the first; a record unregistered during the update keeps its place until the next sort (it only loses its collidable);
//   * UpdateCoord: no collidable -> 1; the same group and in the lists -> 0 with `reinsert`; the bounds from slots 0 / 1, a degenerate box (not min < max
//     on every axis) becomes min .. min + 1; the score 1 for a move of at most 50 (RW 0xBD88C4) in x or y, else 11 - _ftol2((d - 50) * -0.01f) (RW 0xBDD74C),
//     above 10000 (u32) 10000.
// Not kept (no observable effect): retail's free lists and its candidate chain order (a candidate is only found by its key).

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

class StateHasher;
class Xfer;

class CollisionManager
{
public:
	struct Record;

	// the collidable (Object + 0x70, vtable RW 0xC12368) of a registered object
	class Collidable
	{
	public:
		virtual ~Collidable() = default;
		virtual void collisionMinCorner(Coord3D &out) const = 0; ///< slot 0
		virtual void collisionMaxCorner(Coord3D &out) const = 0; ///< slot 1
		virtual std::uint32_t collisionId() const = 0;          ///< slot 4
		virtual std::uint32_t collisionGroup() const = 0;       ///< slot 5 (0: none)
		virtual bool collisionTest(Collidable &other) = 0;       ///< slot 8
		virtual void collisionRespond(Collidable &other) = 0;    ///< slot 9
		Record *collisionRecord = nullptr;                       ///< slots 6 / 7 (Object + 0x4EC)
	};

	struct Endpoint
	{
		Endpoint *prev = nullptr;
		Endpoint *next = nullptr;
		Record *record = nullptr;
		unsigned kind = 0; ///< bit 0: the end; >> 1: the axis
		float value = 0.0f;
		bool isEnd() const { return (kind & 1u) != 0; }
	};
	struct Contact;
	struct Candidate
	{
		Record *first = nullptr;
		Record *second = nullptr;
		std::uint32_t idA = 0, idB = 0;
		unsigned counter = 0;
	};
	struct Record
	{
		Collidable *source = nullptr;
		std::uint32_t id = 0;
		std::uint32_t group = 0;
		Endpoint ep[6]; ///< 0 end x, 1 start x, 2 end y, 3 start y, 4 end z, 5 start z
		bool dirty = false;
		Record *dirtyNext = nullptr, *dirtyPrev = nullptr;
		std::vector<std::uint64_t> pairs; ///< + 0x20: the keys of its candidates (newest last)
		Record *openNext = nullptr, *openPrev = nullptr;
		bool open = false;
	};
	struct Contact
	{
		Record *first = nullptr;
		Record *second = nullptr;
		std::uint32_t idA = 0, idB = 0;
		int state = 0;
		Contact *next = nullptr, *prev = nullptr;
		unsigned bucket = 0;
	};

	CollisionManager();
	~CollisionManager();
	CollisionManager(const CollisionManager &) = delete;
	CollisionManager &operator=(const CollisionManager &) = delete;

	void registerObject(Collidable &c);   ///< RW 0xB6CB80
	void unregisterObject(Collidable &c); ///< RW 0xB6D560
	void markDirty(Collidable &c);        ///< RW 0xB6C700
	void update();                        ///< RW 0xB6E6B0
	void reset();                         ///< every record, pair and contact freed

	size_t recordCount() const { return m_recordCount; }
	size_t contactCount() const;
	bool hasContact(std::uint32_t a, std::uint32_t b) const;
	unsigned long long tests() const { return m_tests; }
	unsigned long long responses() const { return m_responses; }
	unsigned long long rebuilds() const { return m_rebuilds; }
	// the state hash: the record and dirty counts, the contacts in the walk's order with their states (the order of the responses)
	void crc(StateHasher &h) const;
	// the port's save layout (retail's CHUNK_Collision keeps nothing a load could not rebuild; a rebuilt manager meets its contacts in another order, so
	// the port saves its whole state: the records, each axis list's order, the candidates, the contacts in their chains, the dirty list). A load keeps the
	// data until restore() binds the records to the loaded objects' collidables by id
	void xfer(Xfer &x);
	void restore(const std::function<Collidable *(std::uint32_t id)> &find);
	// the contacts in the walk's order (diagnostics, tests)
	std::vector<std::pair<std::uint32_t, std::uint32_t>> contactOrder() const;

	static constexpr unsigned kCandidateBuckets = 0x2B7B;
	static constexpr unsigned kContactBuckets = 0x493;

private:
	unsigned updateCoord(Record &r, bool reinsert); // RW 0xB6C7D0
	void prune();                                  // RW 0xB6CF90
	void sort();                                   // RW 0xB6D8A0
	void walk();                                   // RW 0xB6D060
	void addOverlap(Record *a, Record *b);         // RW 0xB6D710
	void removeOverlap(Record *a, Record *b);
	void unlinkChain(Record &r);                   // RW 0xB6D4A0
	void insertSorted(Record &r, unsigned axis);
	void pushFront(Endpoint &e, unsigned axis);
	void unlinkEndpoint(Endpoint &e, unsigned axis);
	void mergeSort(unsigned axis);                 // RW 0xB6C970 (BFME2 0x7586F0)
	void rebuild();
	void dropDirty(Record &r);
	void pushDirty(Record &r);
	void openPush(Record *&head, Record *r);
	void openRemove(Record *&head, Record *r);
	void insertContact(Record *a, Record *b, std::uint32_t idA, std::uint32_t idB);
	void removeContact(std::uint32_t idA, std::uint32_t idB);
	void unlinkContact(Contact *c);
	void destroyRecord(Record *r);
	static std::uint64_t key(std::uint32_t a, std::uint32_t b) { return ((std::uint64_t)a << 32) | b; }

	std::vector<Record *> m_records;                 ///< every record (retail's list at + 0; only its count is read)
	size_t m_recordCount = 0;                        ///< + 4
	Record *m_dirty = nullptr;                       ///< + 8
	Endpoint *m_axisHeads[3] = { nullptr, nullptr, nullptr };
	std::unordered_map<std::uint64_t, Candidate> m_candidates; ///< the 0x2B7B buckets (+ 0x18)
	Contact *m_buckets[kContactBuckets] = {};        ///< + 0xAE10
	std::unordered_map<std::uint64_t, Contact *> m_contactByKey; ///< retail finds a contact by its bucket chain
	unsigned m_index = 0;                            ///< + 0xC060
	Contact *m_cursor = nullptr;                     ///< + 0xC064
	unsigned m_axisCount = 3;                        ///< + 0xC068
	bool m_rebuild = false;                          ///< + 0xC06C
	bool m_busy = false;                             ///< + 0xC06D
	unsigned long long m_tests = 0, m_responses = 0, m_rebuilds = 0;
	struct Saved
	{
		struct Rec
		{
			std::uint32_t id = 0, group = 0;
			float values[6] = {};
			bool inLists = false;
		};
		std::vector<Rec> records;
		std::vector<std::pair<std::uint32_t, std::uint8_t>> axes[3];
		std::vector<Candidate> candidates;
		std::vector<std::vector<std::pair<std::pair<std::uint32_t, std::uint32_t>, int>>> buckets;
		std::vector<std::uint32_t> dirty;
		bool rebuild = false;
		bool loaded = false;
	};
	Saved m_saved;
};
