// OpenBFME unit tests. GPL-3.0.
// Lane PERF-2: the benchmark of the parallel logic (skipped by default: openbfme_tests -tc="perf2 bench*" --no-skip). PERF-1's big battle (about 1080
// objects) with the logic job pool at 1 thread and at OPENBFME_BENCH_THREADS (default: the hardware threads), each frame's logic step and state hash
// timed on the wall clock (several threads work on them) and the process CPU time per frame (cores busy = CPU / wall). Prints a line per thread count
// and checks that every frame's hash is the same at every thread count. OPENBFME_BENCH_FRAMES (default 400).

#include "doctest.h"

#include "PerfBenchUtil.h"
#include "PerfScenarios.h"
#include "StartTestUtil.h"

#include "Common/JobSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "Common/StateHash.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace
{
double wallMs()
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double processCpuMs()
{
#ifdef _WIN32
	return wallMs();
#else
	timespec ts{};
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
#endif
}

struct BenchRun
{
	std::vector<double> logicMs, hashMs;
	double cpuMs = 0.0, wallTotalMs = 0.0;
	std::vector<std::uint32_t> hashes;
};

BenchRun runBattle(hudtest::SharedWorld &sh, int frames)
{
	BenchRun out;
	auto scope = sh.world->enterContext();
	hudtest::Rig rig(sh);
	GameLogic &logic = rig.logic();
	perf1::setUpBigBattle(rig);
	const double c0 = processCpuMs(), w0 = wallMs();
	for (int f = 0; f < frames; ++f)
	{
		const double t0 = wallMs();
		logic.runLogicFrame();
		const double t1 = wallMs();
		out.hashes.push_back(logic.computeStateHash());
		out.logicMs.push_back(t1 - t0);
		out.hashMs.push_back(wallMs() - t1);
	}
	out.cpuMs = processCpuMs() - c0;
	out.wallTotalMs = wallMs() - w0;
	return out;
}
} // namespace

TEST_CASE("perf2 bench: the big battle's logic step and state hash on the wall clock with the logic job pool at 1 thread and at N threads" * doctest::skip())
{
	if (!hudtest::haveWorld("perf2 bench"))
	{
		return;
	}
	const int frames = perfbench::envInt("OPENBFME_BENCH_FRAMES", 400);
	const int n = perfbench::envInt("OPENBFME_BENCH_THREADS", JobSystem::defaultThreadCount());
	const int before = JobSystem::logic().threadCount();
	std::vector<std::uint32_t> reference;
	for (int threads : { 1, n, 1, n })
	{
		JobSystem::logic().setThreadCount(threads);
		const BenchRun r = runBattle(hudtest::shared(), frames);
		const perfbench::FrameStats l = perfbench::stats(r.logicMs), h = perfbench::stats(r.hashMs);
		std::printf("  info: perf2 bench big battle %d frames, %2d logic threads: logic step (wall) mean %.3f ms p95 %.3f ms max %.3f ms; state hash (wall) mean "
		            "%.3f ms p95 %.3f ms; process CPU %.3f ms per frame over %.3f ms wall per frame (%.2f cores busy); hash chain %08x\n",
			frames, threads, l.mean, l.p95, l.max, h.mean, h.p95, r.cpuMs / frames, r.wallTotalMs / frames, r.cpuMs / r.wallTotalMs,
			(unsigned)perfbench::chain(r.hashes));
		if (reference.empty())
		{
			reference = r.hashes;
		}
		CHECK(r.hashes == reference);
	}
	JobSystem::logic().setThreadCount(before);
}

TEST_CASE("perf2 bench: the job system's dispatch cost and the scaling of a compute-bound loop with 1 .. N threads" * doctest::skip())
{
	std::vector<int> counts = { 1, 2, 3, 4, 6, 8 };
	if (JobSystem::defaultThreadCount() > 8)
	{
		counts.push_back(JobSystem::defaultThreadCount());
	}
	for (int threads : counts)
	{
		JobSystem pool(threads, JobSystem::FloatingPoint::Simulation, "bench");
		// an empty loop of 64 chunks: the hand-over alone
		const int dispatches = 2000;
		double t0 = wallMs();
		for (int i = 0; i < dispatches; ++i)
		{
			pool.parallelFor(64, 1, [](size_t, size_t, size_t) {});
		}
		const double emptyUs = (wallMs() - t0) * 1000.0 / dispatches;
		// 64 chunks of pure arithmetic (about 1 ms in all on one thread)
		std::vector<std::uint32_t> out(64);
		std::vector<double> ms;
		for (int rep = 0; rep < 50; ++rep)
		{
			t0 = wallMs();
			pool.parallelFor(64, 1, [&out](size_t chunk, size_t, size_t) {
				std::uint32_t x = (std::uint32_t)chunk + 1;
				for (int i = 0; i < 60000; ++i)
				{
					x = x * 1664525u + 1013904223u;
					x ^= x >> 13;
				}
				out[chunk] = x;
			});
			ms.push_back(wallMs() - t0);
		}
		std::sort(ms.begin(), ms.end());
		std::printf("  info: perf2 bench jobs, %2d threads: empty dispatch of 64 chunks %.1f us; compute loop p50 %.3f ms, min %.3f ms\n", threads, emptyUs, ms[ms.size() / 2],
			ms.front());
	}
}

TEST_CASE("perf2 bench: the skirmish map load (LiveGame::load of map mp fall back 4p, 4 computer players) with the job pools at 1 thread and at N" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	const int n = perfbench::envInt("OPENBFME_BENCH_THREADS", JobSystem::defaultThreadCount());
	const int logicBefore = JobSystem::logic().threadCount(), clientBefore = JobSystem::client().threadCount();
	for (int threads : { 1, n, 1, n, 1, n })
	{
		JobSystem::logic().setThreadCount(threads);
		JobSystem::client().setThreadCount(threads);
		perf1::Run run;
		perf1::runAISkirmish(*s->mount->fs, *s->world, s->options, s->settings, perf1::fourFactions(), 1, run);
		std::printf("  info: perf2 bench skirmish map load, %2d threads: %.1f ms (%zu objects)\n", threads, run.loadMs, run.objectsStart);
	}
	JobSystem::logic().setThreadCount(logicBefore);
	JobSystem::client().setThreadCount(clientBefore);
}

// Fork lane CRCFREQ-1 (plan phase 1, item 3): what the network's CRC frame costs, before the multiplayer CRC interval goes from 100 frames (20 s) to
// 10 (2 s). A CRC frame computes GameLogic::computeStateHashBreakdown (LiveGame::captureCompletion, every crcInterval frames: the hash, its sections and
// every object's hash, kept for the exchange after a mismatch); every other frame of a recorded game (every network game is recorded) computes the plain
// computeStateHash. OPENBFME_BENCH_ARMIES copies of the big battle (default 3: about 3200 objects, a late 4v4) for OPENBFME_BENCH_FRAMES frames
// (default 200); each frame's logic step, plain hash and breakdown are timed on the wall clock. Prints the breakdown's extra cost over the plain hash
// per frame at intervals 100 and 10, as a share of the mean logic step and of the 200 ms frame budget.
TEST_CASE("perf2 bench: the CRC frame's state hash breakdown against the plain hash, at about 3200 objects" * doctest::skip())
{
	if (!hudtest::haveWorld("perf2 bench crc"))
	{
		return;
	}
	const int frames = perfbench::envInt("OPENBFME_BENCH_FRAMES", 200);
	const int armies = perfbench::envInt("OPENBFME_BENCH_ARMIES", 3);
	auto scope = hudtest::shared().world->enterContext();
	hudtest::Rig rig(hudtest::shared());
	GameLogic &logic = rig.logic();
	for (int i = 0; i < armies; ++i)
	{
		perf1::setUpBigBattle(rig);
	}
	size_t objects = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		++objects;
	}
	std::vector<double> logicMs, hashMs, breakdownMs;
	std::vector<GameLogic::StateHashSection> sections;
	std::vector<GameLogic::ObjectStateHash> perObject;
	for (int f = 0; f < frames; ++f)
	{
		const double t0 = wallMs();
		logic.runLogicFrame();
		const double t1 = wallMs();
		const std::uint32_t plain = logic.computeStateHash();
		const double t2 = wallMs();
		sections.clear();
		perObject.clear();
		const std::uint32_t total = logic.computeStateHashBreakdown(sections, &perObject);
		const double t3 = wallMs();
		REQUIRE(total == plain); // the breakdown's total is the plain hash (GameLogic.h)
		logicMs.push_back(t1 - t0);
		hashMs.push_back(t2 - t1);
		breakdownMs.push_back(t3 - t2);
	}
	const perfbench::FrameStats l = perfbench::stats(logicMs), h = perfbench::stats(hashMs), b = perfbench::stats(breakdownMs);
	const double extra = b.mean - h.mean;
	std::printf("  info: crc bench %d armies, %zu objects at the start, %d frames: logic step mean %.3f ms p95 %.3f ms; plain hash mean %.3f ms p95 %.3f ms; "
	            "breakdown mean %.3f ms p95 %.3f ms max %.3f ms (%zu sections, %zu objects)\n",
		armies, objects, frames, l.mean, l.p95, h.mean, h.p95, b.mean, b.p95, b.max, sections.size(), perObject.size());
	for (int interval : { 100, 10 })
	{
		const double perFrame = extra / interval;
		std::printf("  info: crc bench interval %3d: the breakdown's extra cost over the plain hash %.4f ms per frame = %.3f%% of the mean logic step, %.4f%% of "
		            "the 200 ms frame; the CRC frame itself %.3f ms over the plain hash\n",
			interval, perFrame, 100.0 * perFrame / l.mean, 100.0 * perFrame / 200.0, extra);
	}
}

// Fork lane CRCFREQ-1: the breakdown's per-object hashes come from the hash's own (parallel) work, no second serial walk. Each is still exactly the
// object's own crc fed into a fresh hasher, in object list order, and the total is the plain hash, in both profiles.
TEST_CASE("crcfreq1 retail: the state hash breakdown's object hashes are every object's own crc, in list order (Faithful and Uber)")
{
	if (!hudtest::haveWorld("crcfreq1 breakdown"))
	{
		return;
	}
	for (SimulationProfile profile : { SimulationProfile::Faithful, SimulationProfile::Uber })
	{
		auto scope = hudtest::shared().world->enterContext();
		hudtest::Rig rig(hudtest::shared());
		GameLogic &logic = rig.logic();
		logic.setSimulationProfile(profile);
		perf1::setUpBigBattle(rig);
		for (int f = 0; f < 20; ++f)
		{
			logic.runLogicFrame();
		}
		std::vector<GameLogic::StateHashSection> sections;
		std::vector<GameLogic::ObjectStateHash> objects;
		const std::uint32_t total = logic.computeStateHashBreakdown(sections, &objects);
		CHECK(total == logic.computeStateHash());
		size_t count = 0;
		for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			++count;
		}
		REQUIRE(objects.size() == count); // one entry per object
		size_t i = 0;
		for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject(), ++i)
		{
			StateHasher own;
			o->crc(own);
			CHECK(objects[i].id == o->getID());
			CHECK(objects[i].value == own.value());
		}
		CHECK(count > 1000); // tolerance: the battle's armies (about 1080 objects, fewer after 20 frames of fighting); a near-empty world proves nothing
	}
}
