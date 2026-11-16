// OpenBFME unit tests. GPL-3.0.
// Fork lane AOD-1 part 2: AODHordeContain (GameLogic/Object/Contain/AODHordeContain.h, RotWK RW 0x87D55F .. 0x87EBEE) on retail data: the Army of the
// Dead's small horde (RohanOathbreakerHordeSmall) on Evendim: its members, the sway records (one per slot, six draws each), the rank lines from the
// slots' distinct X, the trail growing as the horde walks, the ghosts kept near the horde, a LARGE unit met (interface slot 0x130), and the save.

#include "doctest.h"

#include "BuildTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/OathbreakerModules.h"
#include "GameLogic/Object/Contain/AODHordeContain.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <string>
#include <vector>

namespace
{
// the lobby's result: slot 0 a human (`faction0`), slot 1 a computer (`faction1`) on Evendim, start positions 0 and 4 (test_pickup_modules.cpp's game)
bool startGame(starttest::Shared &s, std::uint32_t seed, buildtest::Game &out, std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, "FactionMen");
	b.tmpl = buildtest::templateIndex(s, "FactionMordor");
	a.color = 0;
	b.color = 1;
	a.start = 0;
	b.start = 4;
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.slots[1].state = SLOT_EASY_AI;
	message.game.mapName = "maps/map mp evendim/map mp evendim.map";
	message.game.seed = seed;
	message.game.startingCash = 5000;
	if (!NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, error))
	{
		return false;
	}
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	return out.live->load(o, error);
}

const Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

Object *firstOf(GameLogic &logic, const Player *owner, const char *kindOfName)
{
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == owner && x->isKindOfName(kindOfName) && !x->isEffectivelyDead())
		{
			return x;
		}
	}
	return nullptr;
}

AODHordeContain *aodOf(Object &o)
{
	return dynamic_cast<AODHordeContain *>(o.findModule("AODHordeContain"));
}

float farthestMember(const Object &horde, const AODHordeContain &c)
{
	float worst = 0.0f;
	for (const Object *m : *c.getContainedItemsList())
	{
		const float d = SimMath::length2d(m->getPosition()->x - horde.getPosition()->x, m->getPosition()->y - horde.getPosition()->y);
		worst = d > worst ? d : worst;
	}
	return worst;
}

struct AodGame
{
	buildtest::Game g;
	Object *horde = nullptr;
	AODHordeContain *aod = nullptr;
	const Player *human = nullptr;
	const Player *computer = nullptr;
};

void startWithGhosts(starttest::Shared &s, AodGame &a, std::uint32_t seed)
{
	std::string err;
	REQUIRE_MESSAGE(startGame(s, seed, a.g, &err), err);
	GameLogic &logic = a.g.live->logic();
	a.human = playerAtStart(logic, 0);
	a.computer = playerAtStart(logic, 4);
	REQUIRE(a.human);
	REQUIRE(a.computer);
	Object *fortress = firstOf(logic, a.human, "COMMANDCENTER");
	REQUIRE(fortress);
	const Coord3D at{ fortress->getPosition()->x + 500.0f, fortress->getPosition()->y, fortress->getPosition()->z };
	a.horde = a.g.live->createObject("RohanOathbreakerHordeSmall", a.human->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(a.horde, err);
	a.aod = aodOf(*a.horde);
	REQUIRE(a.aod);
}
} // namespace

TEST_CASE("aod1 retail: the Army of the Dead's small horde has its ghosts, one sway record per slot, the rank lines and the trail (RW 0x87E8FC)")
{
	OPENBFME_REQUIRE_START(s);
	AodGame a;
	startWithGhosts(*s, a, 2101);
	for (int i = 0; i < 10; ++i)
	{
		a.g.live->stepFrame();
	}
	const AODHordeContain &c = *a.aod;
	MESSAGE("ghosts " << c.getContainCount() << ", slots " << c.getSlotCount() << ", sway " << c.sway().size() << ", rank lines " << c.rankLineCount() << ", trail "
	                  << c.trailCount() << ", farthest " << farthestMember(*a.horde, c));
	CHECK(c.getContainCount() > 0u); // tolerance: OATHBREAKER_SMALL_HORDE_SIZE is the data's macro; the members exist
	CHECK(c.sway().size() == c.getSlotCount());
	// the small horde's RankInfo X: 10, -10, -30, -50 (RankNumber 1 and three RankNumber 2 lines), centred by HordeContain's buildSlots (RW 0x86C4EA): + 20
	REQUIRE(c.rankLineCount() == 4);
	CHECK(c.rankLine(0).dist == 30.0f);
	CHECK(c.rankLine(1).dist == 10.0f);
	CHECK(c.rankLine(2).dist == -10.0f);
	CHECK(c.rankLine(3).dist == -30.0f);
	CHECK(c.trailCount() >= 4); // tolerance: the four rank lines seed it; a frame may add the horde's position
	for (const std::array<float, 6> &r : c.sway())
	{
		// the data: AmplitudeRandomness 0.4, FrequencyRandomness 0.3, the Z twins 0.2 / 0.4
		CHECK(r[1] >= 0.6f); // tolerance: a draw within 1 +- AmplitudeRandomness
		CHECK(r[1] <= 1.4f); // tolerance: as above
		CHECK(r[2] >= 0.7f); // tolerance: 1 +- FrequencyRandomness
		CHECK(r[2] <= 1.3f); // tolerance: as above
	}
	CHECK(farthestMember(*a.horde, c) < 120.0f); // tolerance: the ghosts float around their rank lines (the horde is 60 deep, the sway 20 across)
}

TEST_CASE("aod1 retail: the ghosts follow the horde's trail when it walks; a LARGE unit met is followed; the save carries the state")
{
	OPENBFME_REQUIRE_START(s);
	AodGame a;
	startWithGhosts(*s, a, 2102);
	GameLogic &logic = a.g.live->logic();
	for (int i = 0; i < 5; ++i)
	{
		a.g.live->stepFrame();
	}
	const int trailBefore = a.aod->trailCount();
	AIUpdateInterface *ai = a.horde->getAIUpdateInterface();
	REQUIRE(ai);
	const Coord3D goal{ a.horde->getPosition()->x + 400.0f, a.horde->getPosition()->y + 200.0f, a.horde->getPosition()->z };
	ai->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	for (int i = 0; i < 60; ++i)
	{
		a.g.live->stepFrame();
	}
	MESSAGE("trail " << trailBefore << " -> " << a.aod->trailCount() << ", farthest ghost " << farthestMember(*a.horde, *a.aod));
	CHECK(a.aod->trailCount() > trailBefore);             // tolerance: the walk adds points (how many depends on the route)
	CHECK(farthestMember(*a.horde, *a.aod) < 160.0f);     // tolerance: the ghosts stay with the horde along the trail
	// a LARGE unit (the mumakil: DeadCollideSize LARGE) met by a member is followed; a small one is not
	std::string err;
	const Coord3D near{ a.horde->getPosition()->x + 60.0f, a.horde->getPosition()->y, a.horde->getPosition()->z };
	Object *mumak = a.g.live->createObject("MordorMumakil", a.computer->getPlayerIndex(), near, 0.0f, &err);
	REQUIRE_MESSAGE(mumak, err);
	Object *ghost = a.aod->getContainedItemsList()->front();
	a.aod->noteMemberContact(ghost, a.horde); // the horde itself: SMALL
	CHECK(a.aod->largeUnit() == INVALID_ID);
	a.aod->noteMemberContact(ghost, mumak);
	CHECK(a.aod->largeUnit() == mumak->getID());
	a.g.live->stepFrame();
	// the save: the module's bytes reload to the same state hash
	StateHasher before;
	a.aod->crc(before);
	XferSave save;
	a.aod->xfer(save);
	XferLoad load(save.data());
	a.aod->xfer(load);
	StateHasher after;
	a.aod->crc(after);
	CHECK(after.value() == before.value());
	(void)logic;
}
