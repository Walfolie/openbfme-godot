// OpenBFME unit tests. GPL-3.0.
// Fork lane COLLIDE-1: the collision manager library (GameLogic/Object/CollisionManager.h, RotWK RW 0xB6C420 .. 0xB6E6B0) on synthetic boxes: the contacts
// of overlapping boxes on all three axes, touching boxes that do not overlap, the group rule, the walk's bucket order, the cached test, a move by the
// insertion sort and by the rebuild, and an unregistration during the walk.

#include "doctest.h"

#include "GameLogic/Object/CollisionManager.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
struct Box : CollisionManager::Collidable
{
	std::uint32_t id = 0;
	std::uint32_t group = 0;
	Coord3D lo{}, hi{};
	bool testAnswer = true;
	int tests = 0;
	std::vector<std::uint32_t> *log = nullptr;
	CollisionManager *unregisterOnRespond = nullptr;
	Box *victim = nullptr;

	void collisionMinCorner(Coord3D &out) const override { out = lo; }
	void collisionMaxCorner(Coord3D &out) const override { out = hi; }
	std::uint32_t collisionId() const override { return id; }
	std::uint32_t collisionGroup() const override { return group; }
	bool collisionTest(Collidable &) override
	{
		++tests;
		return testAnswer;
	}
	void collisionRespond(Collidable &other) override
	{
		if (log)
		{
			log->push_back(id * 1000u + static_cast<Box &>(other).id);
		}
		if (unregisterOnRespond && victim)
		{
			unregisterOnRespond->unregisterObject(*victim);
		}
	}
	void at(float x, float y, float r = 5.0f)
	{
		lo = Coord3D{ x - r, y - r, 0.0f };
		hi = Coord3D{ x + r, y + r, 10.0f };
	}
};

std::unique_ptr<Box> box(std::uint32_t id, float x, float y, float r = 5.0f)
{
	auto b = std::make_unique<Box>();
	b->id = id;
	b->at(x, y, r);
	return b;
}
} // namespace

TEST_CASE("collide1: overlapping boxes make one contact, the lower id responding first; boxes apart make none (RW 0xB6D710, 0xB6D060)")
{
	CollisionManager m;
	std::vector<std::uint32_t> log;
	auto a = box(7, 100.0f, 100.0f), b = box(3, 108.0f, 100.0f), c = box(9, 300.0f, 300.0f), d = box(12, 119.0f, 100.0f);
	for (Box *x : { a.get(), b.get(), c.get(), d.get() })
	{
		x->log = &log;
		m.registerObject(*x);
	}
	m.update();
	CHECK(m.recordCount() == 4u);
	CHECK(m.hasContact(3, 7));
	CHECK(m.rebuilds() == 1u); // a new record scores its distance from 0: the first frame always rebuilds
	CHECK_FALSE(m.hasContact(3, 12)); // 103 .. 113 and 114 .. 124
	CHECK_FALSE(m.hasContact(3, 9));
	CHECK(m.contactCount() == 1u);
	CHECK(log == std::vector<std::uint32_t>{ 3007u, 7003u });
	CHECK(b->tests == 1); // the first (lower id) tests
	CHECK(a->tests == 0);
	// nobody moved: the contact keeps its answer (no second test), and responds again
	log.clear();
	m.update();
	CHECK(b->tests == 1);
	CHECK(log == std::vector<std::uint32_t>{ 3007u, 7003u });
	// moved: tested again; a test that says no keeps the contact but no response
	b->testAnswer = false;
	b->at(108.5f, 100.0f);
	m.markDirty(*b);
	log.clear();
	m.update();
	CHECK(b->tests == 2);
	CHECK(log.empty());
	CHECK(m.hasContact(3, 7));
}

TEST_CASE("collide1: two records of one non-zero group never pair (two IMMOBILE objects, RW 0x68EEA5)")
{
	CollisionManager m;
	auto a = box(1, 100.0f, 100.0f), b = box(2, 104.0f, 100.0f), c = box(3, 102.0f, 100.0f);
	a->group = 1;
	b->group = 1;
	m.registerObject(*a);
	m.registerObject(*b);
	m.registerObject(*c);
	m.update();
	CHECK_FALSE(m.hasContact(1, 2));
	CHECK(m.hasContact(1, 3));
	CHECK(m.hasContact(2, 3));
}

TEST_CASE("collide1: the walk follows the contact buckets ((a << 16) + b) % 0x493 in order (RW 0xB6D060)")
{
	CollisionManager m;
	std::vector<std::unique_ptr<Box>> boxes;
	std::vector<std::uint32_t> log;
	// a stack of four overlapping boxes: six contacts
	for (std::uint32_t id : { 5u, 1u, 900u, 70000u })
	{
		boxes.push_back(box(id, 100.0f, 100.0f));
		boxes.back()->log = &log;
		m.registerObject(*boxes.back());
	}
	m.update();
	const auto order = m.contactOrder();
	REQUIRE(order.size() == 6u);
	for (size_t i = 1; i < order.size(); ++i)
	{
		const unsigned prev = (unsigned)(((order[i - 1].first << 16) + order[i - 1].second) % 0x493);
		const unsigned cur = (unsigned)(((order[i].first << 16) + order[i].second) % 0x493);
		CHECK(prev <= cur); // tolerance: a bucket holds several contacts; the order across the buckets is what is under test
	}
	CHECK(log.size() == 12u);
}

TEST_CASE("collide1: a move is followed by the insertion sort, a big one by the rebuild; the pairs come and go (RW 0xB6D8A0)")
{
	CollisionManager m;
	std::vector<std::unique_ptr<Box>> boxes;
	for (std::uint32_t id = 1; id <= 6; ++id)
	{
		boxes.push_back(box(id, 20.0f * (float)id, 100.0f));
		m.registerObject(*boxes.back());
	}
	m.update();
	CHECK(m.contactCount() == 0u);
	CHECK(m.rebuilds() == 1u); // the first frame
	// a small step (12 <= 50 scores 1; a total of 1 against 6 records): box 2 overlaps box 3 through the insertion sort
	boxes[1]->at(52.0f, 100.0f);
	m.markDirty(*boxes[1]);
	m.update();
	CHECK(m.hasContact(2, 3));
	CHECK(m.rebuilds() == 1u);
	// every box jumps about 4900 (each scores 11 + 48): the total passes the record count -> the rebuild
	for (size_t i = 0; i < boxes.size(); ++i)
	{
		boxes[i]->at(5000.0f + 4.0f * (float)i, 100.0f);
		m.markDirty(*boxes[i]);
	}
	m.update();
	CHECK(m.rebuilds() == 2u);
	CHECK(m.contactCount() == 9u); // 4 apart, 10 wide: the neighbours one and two along
	// apart again by small steps (at most 40: a score of 1 each, 6 against 6): the contacts end through the insertion sort
	for (size_t i = 0; i < boxes.size(); ++i)
	{
		boxes[i]->at(5000.0f + 12.0f * (float)i, 100.0f);
		m.markDirty(*boxes[i]);
	}
	m.update();
	CHECK(m.rebuilds() == 2u);
	CHECK(m.contactCount() == 0u);
}

TEST_CASE("collide1: an object unregistered during the walk keeps its record until the next sort and gets no response (RW 0xB6D560)")
{
	CollisionManager m;
	std::vector<std::uint32_t> log;
	auto a = box(1, 100.0f, 100.0f), b = box(2, 102.0f, 100.0f), c = box(3, 104.0f, 100.0f);
	for (Box *x : { a.get(), b.get(), c.get() })
	{
		x->log = &log;
		m.registerObject(*x);
	}
	a->unregisterOnRespond = &m;
	a->victim = c.get();
	m.update();
	CHECK(c->collisionRecord == nullptr);
	CHECK(m.recordCount() == 3u); // still there until the next sort
	// the buckets: (2, 3) 1094, (1, 2) 1133, (1, 3) 1134; 1's response to 2 unregisters 3, so the contact (1, 3) is skipped
	CHECK(log == std::vector<std::uint32_t>{ 2003u, 3002u, 1002u, 2001u });
	a->unregisterOnRespond = nullptr;
	m.update();
	CHECK(m.recordCount() == 2u);
	CHECK_FALSE(m.hasContact(1, 3));
	CHECK(m.hasContact(1, 2));
}
