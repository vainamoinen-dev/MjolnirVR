// core_platform_tests.cpp: sig::Find, camera recovery, the capture and co-op probes, pools, defaults and roomscale input.
// Units 3, 21, 22, 23, 101, 108, 193, 235, 239, 241, 249, 251, 253 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_platform_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(3);
    Check(PadFirstAllowsMouseMode(true) && !PadFirstAllowsMouseMode(false),
        "pad-first: mouse mode allowed for the whole pause presentation, refused otherwise");
    CheckUnit(21);
    {
        using namespace weapon_order_diagnostic;
        const uint64_t packed = (uint64_t(10) << 32) | 7;
        Check(CaptureProbePermission(true, true, packed, /*storedSession=*/5,
                  /*currentSession=*/5, /*currentThread=*/7) == kProbePermissionAllowed,
            "Capture probe is authorized by title, generation, session and thread");
        Check(CaptureProbePermission(true, true, packed, 5, /*currentSession=*/6, 7) == kProbePermissionNoSafeThread,
            "A new recording session never inherits the previous session's FP thread authorization even when the numeric thread id matches");
        Check(CaptureProbePermission(true, true, /*storedPacked=*/0, 0, 5, 7) == kProbePermissionNoSafeThread,
            "No observed FP thread means no Capture probe attempt");
        Check(CaptureProbePermission(false, true, packed, 5, 5, 7) == kProbePermissionGuardRejected &&
                  CaptureProbePermission(true, false, packed, 5, 5, 7) == kProbePermissionGuardRejected,
            "Title or generation mismatch is a lifecycle guard rejection");
        Check(CaptureProbePermission(true, true, packed, 5, 5, /*currentThread=*/8) == kProbePermissionThreadMismatch,
            "A foreign Capture thread is refused rather than reading TLS");
    }

    CheckUnit(22);
    {
        // sig::Find is memchr-anchored for speed (it is the dominant cost of
        // every hook install: ODST resolves four optional features, each
        // needing a find plus an ambiguity re-scan, ~945 ms of whole-module
        // passes measured 2026-08-06). Speed is worthless if it changes WHICH
        // address is returned, so every case here is checked against the
        // original byte-by-byte loop kept in ReferenceFind.
        //
        // Deterministic xorshift, not <random>: the corpus must be identical
        // on every machine so a disagreement is always reproducible.
        uint32_t rng = 0x13579BDFu;
        auto NextByte = [&rng]() -> uint8_t
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<uint8_t>(rng >> 24);
        };

        // A tiny alphabet makes accidental partial matches - and therefore
        // candidate positions that must be rejected - overwhelmingly common.
        std::vector<uint8_t> corpus(64 * 1024);
        for (auto& b : corpus)
            b = static_cast<uint8_t>(NextByte() & 0x03);

        bool allAgree = true;
        bool sawHit = false;
        bool sawMiss = false;
        for (int trial = 0; trial < 400; ++trial)
        {
            const size_t n = 1 + (NextByte() % 12);
            std::vector<uint8_t> bytes(n);
            std::vector<uint8_t> wild(n);
            std::string pattern;
            // Half the trials copy a real run out of the corpus (guaranteeing
            // a hit somewhere); half are random (usually a miss).
            const bool fromCorpus = (NextByte() & 1) != 0;
            const size_t origin = NextByte() % (corpus.size() - 16);
            for (size_t j = 0; j < n; ++j)
            {
                // Wildcards at every position, including the first - that is
                // the case the anchor skip has to handle by moving the anchor.
                wild[j] = (NextByte() % 4 == 0) ? 1 : 0;
                bytes[j] = wild[j] ? 0 : (fromCorpus ? corpus[origin + j] : static_cast<uint8_t>(NextByte() & 0x03));
                if (!pattern.empty())
                    pattern += ' ';
                if (wild[j])
                {
                    pattern += "??";
                }
                else
                {
                    static const char* kHex = "0123456789ABCDEF";
                    pattern += kHex[bytes[j] >> 4];
                    pattern += kHex[bytes[j] & 0x0F];
                }
            }
            const uintptr_t base = reinterpret_cast<uintptr_t>(corpus.data());
            const uintptr_t expected = ReferenceFind(corpus.data(), corpus.size(), bytes, wild);
            const uintptr_t actual = sig::Find(base, corpus.size(), pattern.c_str());
            if (expected != actual)
                allAgree = false;
            if (expected)
                sawHit = true;
            else
                sawMiss = true;

            // The ambiguity re-scan callers rely on to prove a signature is
            // unique starts one byte past a hit; it must agree there too.
            if (expected)
            {
                const uintptr_t offset = expected - base + 1;
                const uintptr_t expectedNext =
                    ReferenceFind(corpus.data() + offset, corpus.size() - offset, bytes, wild);
                const uintptr_t actualNext = sig::Find(base + offset, corpus.size() - offset, pattern.c_str());
                if (expectedNext != actualNext)
                    allAgree = false;
            }
        }
        Check(allAgree, "sig::Find matches the byte-by-byte reference exactly");
        Check(sawHit && sawMiss, "sig::Find equivalence corpus covered both hits and misses");

        // All-wildcard: matches at the first offset, as the original did.
        const std::vector<uint8_t> corpusSmall{0xAA, 0xBB, 0xCC};
        const uintptr_t smallBase = reinterpret_cast<uintptr_t>(corpusSmall.data());
        Check(sig::Find(smallBase, corpusSmall.size(), "?? ??") == smallBase,
            "sig::Find all-wildcard pattern matches at the first offset");
        // A pattern longer than the region never matches.
        Check(sig::Find(smallBase, corpusSmall.size(), "AA BB CC DD") == 0,
            "sig::Find rejects a pattern longer than the region");
        // A trailing-wildcard pattern that would run off the end must not
        // match early, and an exact tail match must be found.
        Check(sig::Find(smallBase, corpusSmall.size(), "CC") == smallBase + 2,
            "sig::Find locates a match at the final offset");
    }
    CheckUnit(23);
    {
        // Level-load gate decision core. Each Observe() is one 50 ms worker
        // sample; `true` means the engine's player-view fingerprint changed.
        // Scenarios replay preserved captures of ODST level loads.
        using Gate = LevelLoadGateLogic;

        // Same-title sessions can stop the camera without changing module
        // base/generation. A latched install gate must not strand that epoch.
        Check(!CameraNeedsRecovery(2999, 1000, 1500, true), "camera recovery preserves a recent heartbeat");
        Check(CameraNeedsRecovery(3500, 1000, 1500, true), "camera recovery retires stale same-generation hooks");
        Check(CameraNeedsRecovery(3000, 1000, 0, true), "camera recovery handles no first callback");
        Check(!CameraNeedsRecovery(3500, 1000, 1500, false) && !CameraNeedsRecovery(500, 1000, 1500, true),
            "camera recovery excludes uninstalled and backwards-time observations");
        Check(!CameraNeedsRecovery(3500, 3400, 1500, true), "a replacement install gets its own camera grace interval");
        Gate recovered;
        recovered.Observe(false);
        recovered.Observe(true);
        recovered.Reset();
        for (int i = 0; i < 400; ++i)
            recovered.Observe(false);
        Check(!recovered.IsOpen(), "recovery never installs on a frozen loading screen");
        recovered.Observe(true);
        Check(recovered.IsOpen(), "recovery accepts the new level camera tick");

        // A load bounce from an earlier build, replayed: a Save & Quit leaves ~3 s of the
        // OUTGOING level's dying ticks running when observation begins. The
        // shipped 12-sample "already running" rule opened at 0.6 s and the
        // load bounced. The dying ticks must hold, the loading screen's
        // freeze must hold, and the NEW level's first tick must open.
        Gate bounced;
        bool heldThroughDyingTicks = true;
        for (int i = 0; i < 60; ++i) // 3 s of dying ticks
            heldThroughDyingTicks &= bounced.Observe(true) == Gate::Decision::Hold;
        Check(heldThroughDyingTicks, "gate holds through 3 s of the outgoing level's dying ticks");
        bool heldThroughLoadFreeze = true;
        for (int i = 0; i < 144; ++i) // 7.2 s loading-screen freeze
            heldThroughLoadFreeze &= bounced.Observe(false) == Gate::Decision::Hold;
        Check(heldThroughLoadFreeze, "gate holds through the loading screen freeze");
        Check(
            bounced.Observe(true) == Gate::Decision::OpenFrozenThenTicking, "gate opens on the new level's first tick");

        // The 11,766 ms successful retry: the OLD 12 s unconditional timeout
        // was within 300 ms of installing into this loading screen. There is
        // no timeout now - stillness holds indefinitely and only the tick
        // opens.
        Gate longLoad;
        bool heldThroughLongFreeze = true;
        for (int i = 0; i < 400; ++i) // 20 s of freeze, past any old timeout
            heldThroughLongFreeze &= longLoad.Observe(false) == Gate::Decision::Hold;
        Check(heldThroughLongFreeze, "gate holds a 20 s freeze with no timeout install");
        Check(longLoad.Observe(true) == Gate::Decision::OpenFrozenThenTicking,
            "gate still opens after an arbitrarily long freeze");

        // A menu idle is stillness forever; the gate must simply keep
        // holding (the flat screen layer still shows in the headset).
        Gate menuIdle;
        bool heldThroughMenu = true;
        for (int i = 0; i < 2400; ++i) // 2 minutes in a lobby
            heldThroughMenu &= menuIdle.Observe(false) == Gate::Decision::Hold;
        Check(heldThroughMenu && !menuIdle.IsOpen(), "gate holds a menu idle indefinitely");

        // A title genuinely mid-level when observation begins never
        // freezes: 6 s of uninterrupted change opens, one sample earlier
        // holds. Dying ticks measured <= ~4 s can never reach it.
        Gate running;
        bool heldBeforeThreshold = true;
        for (uint32_t i = 0; i + 1 < Gate::kAlreadyRunningSamples; ++i)
            heldBeforeThreshold &= running.Observe(true) == Gate::Decision::Hold;
        Check(heldBeforeThreshold, "gate holds one sample short of already-running");
        Check(
            running.Observe(true) == Gate::Decision::OpenAlreadyRunning, "gate opens for a title already running 6 s");

        // The frozen half is deliberately satisfied by a SINGLE still
        // sample. In every bounced capture the dying ticks ran with no
        // still sample at all (still=0 at the first gate log line), while
        // the captured pause-resume witnesses exactly one still before
        // gameplay's first tick - a higher minimum (300 ms in an earlier
        // build) sent every resume down the 6 s already-running path and
        // was rejected as far too slow to enter 3D.
        Gate oneStill;
        for (int i = 0; i < 40; ++i)
            oneStill.Observe(true);
        Check(!oneStill.SawFrozen(), "uninterrupted dying ticks never satisfy the frozen half");
        oneStill.Observe(false);
        Check(oneStill.SawFrozen(), "a single witnessed still satisfies the frozen half");
        Check(oneStill.Observe(true) == Gate::Decision::OpenFrozenThenTicking,
            "the next tick after a witnessed still opens the gate");

        // Pause -> resume (the 07:14:13 capture): the pause fade's single
        // witnessed still is the frozen half, the resume tick opens - the
        // ~94 ms resume the accepted builds had.
        Gate resume;
        for (uint32_t i = 0; i < Gate::kFrozenMinSamples; ++i)
            resume.Observe(false);
        Check(resume.Observe(true) == Gate::Decision::OpenFrozenThenTicking,
            "pause-resume reopens on the first tick after stillness");

        // Rearm: once open, Reset() closes it and the proof must be
        // re-earned - an earlier one-shot defect, expressed as a test.
        Gate rearm;
        for (uint32_t i = 0; i < Gate::kFrozenMinSamples; ++i)
            rearm.Observe(false);
        rearm.Observe(true);
        Check(rearm.IsOpen(), "gate open before rearm");
        rearm.Reset();
        Check(!rearm.IsOpen(), "rearm closes the gate");
        bool heldAfterRearm = true;
        for (int i = 0; i < 60; ++i)
            heldAfterRearm &= rearm.Observe(true) == Gate::Decision::Hold;
        Check(heldAfterRearm, "after rearm the dying ticks hold again until a real freeze");
    }
    CheckUnit(101);
    {
        const Config fresh;
        Check(fresh.show_welcome && fresh.fit_desktop_window && fresh.hud_size == 0.43f &&
                  fresh.base_tunables.hud_size == 0.43f && fresh.halo2_classic_gun_yaw_deg == 1.0f &&
                  fresh.halo2_classic_gun_pitch_deg == -9.5f,
            "The V5 seed defaults keep the welcome visible, fit the desktop "
            "window, use the V5 HUD size, and seed the accepted Halo 2 "
            "Classic yaw/pitch alignment");
        Check(fresh.dpad_hand == 0 && fresh.dpad_head_radius == 0.30f && !fresh.quest_thumbrest_dpad,
            "D-pad defaults preserve the existing 30 cm left-hand head gesture and leave Quest thumb-rest mode off");
        Check(fresh.two_hand_coherent_aim, "Coherent committed-sample aim defaults on");
    }

    CheckUnit(108);
    {
        // Every group's keys have an F1 control, and each slider's
        // range sits inside the key's Clamp range (same constants, or numbers inside).
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
        const std::string menu = ReadTextFile(root / "src/dll/core/menu.cpp");
        const std::string config = ReadTextFile(root / "src/common/config.cpp");
        const auto args = [](const std::string& text, size_t at, int count)
        {
            std::vector<std::string> out;
            std::string token;
            int depth = 0;
            for (; at < text.size() && (int)out.size() < count; ++at)
            {
                const char c = text[at];
                if (c == '(')
                    ++depth;
                if (c == ')')
                {
                    if (!depth)
                    {
                        out.push_back(token);
                        break;
                    }
                    --depth;
                }
                if (c == ',' && !depth)
                {
                    out.push_back(token);
                    token.clear();
                    continue;
                }
                if (!std::isspace(static_cast<unsigned char>(c)))
                    token += c;
            }
            return out;
        };
        const auto number = [](std::string t, double& v)
        {
            if (!t.empty() && t.back() == 'f')
                t.pop_back();
            char* end = nullptr;
            v = std::strtod(t.c_str(), &end);
            return end && !*end && !t.empty();
        };
        std::string missing;
        for (const char* key :
            {"reticle_r", "reticle_g", "reticle_b", "kill_reticle", "halo4_hands", "halo4_hands_mirrored",
                "halo4_hand_forward_m", "halo4_hand_vertical_m", "halo4_hand_lateral_m", "halo2_classic_gun_roll_deg",
                "halo2_classic_gun_forward_m", "halo2_classic_gun_right_m", "halo2_classic_gun_up_m",
                "scope_sight_min_distance_m", "vehicle_cam_forward_m", "vehicle_cam_up_m", "vehicle_cam_right_m"})
        {
            const std::string field = std::string("g_config.") + key;
            if (menu.find(field) == std::string::npos)
            {
                missing += std::string(" ") + key;
                continue;
            }
            const size_t slider = menu.find("&" + field + ",");
            if (slider == std::string::npos)
                continue; // a checkbox or the colour picker
            const auto range = args(menu, slider + field.size() + 2, 3);
            const size_t clampAt = config.find("std::clamp(" + field + ",");
            if (range.size() < 2 || clampAt == std::string::npos)
            {
                missing += std::string(" range:") + key;
                continue;
            }
            const auto limits = args(config, clampAt + 12 + field.size(), 2);
            double lo = 0, hi = 0, clo = 0, chi = 0;
            const bool same = limits.size() == 2 && range[0] == limits[0] && range[1] == limits[1];
            const bool inside = limits.size() == 2 && number(range[0], lo) && number(range[1], hi) &&
                                number(limits[0], clo) && number(limits[1], chi) && lo >= clo && hi <= chi;
            if (!same && !inside)
                missing += std::string(" range:") + key;
        }
        if (!missing.empty())
            std::printf("F1 controls missing or out of Clamp's range:%s\n", missing.c_str());
        Check(missing.empty(), "F1 B (A5-8): each group's keys have an F1 control inside Clamp's range");
    }
    CheckUnit(193);
    // The maintainer's headset-tuned seats ARE the product default, so a file
    // that predates a key must still land on their placement rather than on a
    // bare universal trim. Asserted on a fresh Config so it reflects the
    // built-ins, not whatever the file above happened to write.
    {
        Config fresh;
        const int shipScorpionDriver = ConfigSeatTrimSlot(1, 0, false);
        const int shipHogPassenger = ConfigSeatTrimSlot(2, 1, false);
        const int shipProwlerGunner = ConfigSeatTrimSlot(6, 0, true);
        const int shipChopperDriver = ConfigSeatTrimSlot(9, 0, false);
        const int untunedMongoose = ConfigSeatTrimSlot(3, 0, false);
        const bool applied = ConfigSeatCamForward(fresh, shipScorpionDriver) == 0.29f &&
                             ConfigSeatCamUp(fresh, shipScorpionDriver) == 1.50f &&
                             ConfigSeatCamRight(fresh, shipScorpionDriver) == -0.03f &&
                             ConfigSeatCamForward(fresh, shipHogPassenger) == -0.28f &&
                             ConfigSeatCamUp(fresh, shipProwlerGunner) == 0.30f &&
                             ConfigSeatCamForward(fresh, shipChopperDriver) == 0.12f;
        // Only the axes actually moved are set. The chopper driver's lateral
        // and the prowler gunner's forward were never touched, so they must
        // still follow the universal trim and move with it.
        const bool untouchedAxesFollow = !fresh.vehicle_cam_right_set[shipChopperDriver] &&
                                         !fresh.vehicle_cam_forward_set[shipProwlerGunner] &&
                                         ConfigSeatCamRight(fresh, shipChopperDriver) == fresh.vehicle_cam_right_m &&
                                         ConfigSeatCamForward(fresh, shipProwlerGunner) == fresh.vehicle_cam_forward_m;
        // A vehicle nobody tuned keeps a completely bare slot.
        const bool untunedStaysBare = !fresh.vehicle_cam_forward_set[untunedMongoose] &&
                                      !fresh.vehicle_cam_up_set[untunedMongoose] &&
                                      !fresh.vehicle_cam_right_set[untunedMongoose];
        // The follow is off out of the box; the vehicles ship world-locked.
        const bool followShipsOff = !fresh.vehicle_view_follow;
        Check(applied && untouchedAxesFollow && untunedStaysBare && followShipsOff,
            "The maintainer's tuned seat placements are the built-in "
            "defaults, only on the axes they moved, and the view follow "
            "ships off");
    }
    CheckUnit(235);
    {
        bool collision = false;
        for (int a = 0; a < 6; ++a)
            for (int b = a + 1; b < 6; ++b)
                if (kReachObserverCameraReturnRvas[a] == kReachObserverCameraReturnRvas[b])
                    collision = true;
        Check(!collision, "The six observer-camera return addresses are all distinct");
    }

    CheckUnit(239);
    // Co-op drop probe: the reduction that turns a per-frame ring into one line
    // per second must age samples correctly, average and peak them per second,
    // count the frames where the mod held MCC's render thread long enough to
    // matter, and survive a GetTickCount64 wrap mid-capture.
    {
        constexpr size_t kBuckets = 8;
        CoopProbeBucket buckets[kBuckets]{};

        // Two seconds of frames. The older second is quiet; the second right
        // before the dump has one 9 ms hold and one 5 ms hold.
        const uint32_t dumpTick = 100000;
        CoopProbeSample samples[6]{};
        samples[0] = {dumpTick - 1500, 0.5f, 0.05f, 8.3f};
        samples[1] = {dumpTick - 1200, 0.7f, 0.05f, 8.4f};
        samples[2] = {dumpTick - 1100, 0.6f, 0.05f, 8.3f};
        samples[3] = {dumpTick - 800, 9.0f, 0.10f, 20.0f};
        samples[4] = {dumpTick - 400, 5.0f, 0.06f, 9.0f};
        samples[5] = {dumpTick - 100, 1.0f, 0.04f, 8.3f};

        const size_t populated = CoopProbeSummarise(samples, 6, dumpTick, buckets, kBuckets);

        // Buckets are indexed by age: [0] is the second before the dump.
        const bool aged = populated == 2 && buckets[0].frames == 3 && buckets[1].frames == 3;
        const bool peaks =
            buckets[0].holdMaxMs == 9.0f && buckets[1].holdMaxMs == 0.7f && buckets[0].intervalMaxMs == 20.0f;
        const bool thresholds = buckets[0].holdOver4Ms == 2 && buckets[0].holdOver8Ms == 1 &&
                                buckets[1].holdOver4Ms == 0 && buckets[1].holdOver8Ms == 0;
        // (9 + 5 + 1) / 3 == 5, and the quiet second averages (0.5+0.7+0.6)/3.
        const bool averaged =
            buckets[0].holdAvgMs == 5.0f && buckets[1].holdAvgMs > 0.59f && buckets[1].holdAvgMs < 0.61f;

        // Samples older than the bucket window are dropped, not misfiled.
        CoopProbeSample stale[1]{};
        stale[0] = {dumpTick - (uint32_t)(kBuckets * 1000 + 5), 3.0f, 0.0f, 8.3f};
        const bool dropsStale = CoopProbeSummarise(stale, 1, dumpTick, buckets, kBuckets) == 0;

        // A capture spanning the uint32 tick wrap still ages correctly, because
        // the subtraction is modular.
        CoopProbeBucket wrapped[kBuckets]{};
        const uint32_t afterWrap = 1500;
        CoopProbeSample across[2]{};
        // 1756 ms before the dump, i.e. the bucket before last, recorded 256 ms
        // before the counter wrapped through zero.
        across[0] = {0xFFFFFF00u, 2.0f, 0.0f, 8.3f};
        across[1] = {afterWrap - 100, 4.0f, 0.0f, 8.3f};
        const size_t wrapPopulated = CoopProbeSummarise(across, 2, afterWrap, wrapped, kBuckets);
        const bool wrapSafe = wrapPopulated == 2 && wrapped[0].frames == 1 && wrapped[1].frames == 1;

        // Degenerate inputs must not write anything.
        const bool guards = CoopProbeSummarise(nullptr, 4, dumpTick, buckets, kBuckets) == 0 &&
                            CoopProbeSummarise(samples, 6, dumpTick, buckets, 0) == 0;

        Check(aged && peaks && thresholds && averaged && dropsStale && wrapSafe && guards,
            "Co-op probe reduces per-frame holds into per-second buckets, keeps "
            "peaks and over-threshold counts, and is tick-wrap safe");

        // Only a real level teardown carries a run-up worth dumping; menu
        // traffic into Loading does not.
        Check(CoopProbeIsInLevelMode(RuntimeMode::Gameplay) && CoopProbeIsInLevelMode(RuntimeMode::Vehicle) &&
                  CoopProbeIsInLevelMode(RuntimeMode::Turret) && CoopProbeIsInLevelMode(RuntimeMode::Paused) &&
                  !CoopProbeIsInLevelMode(RuntimeMode::Shell) && !CoopProbeIsInLevelMode(RuntimeMode::Loading) &&
                  !CoopProbeIsInLevelMode(RuntimeMode::Unsupported),
            "Co-op probe dumps only when a live level falls to Loading");
    }

    CheckUnit(241);
    {
        // The intermediate-copy pool. The defect this replaces kept ONE
        // intermediate and rebuilt it whenever the requested shape differed,
        // so two different-sized slow-path sources in one frame destroyed and
        // recreated a full-resolution texture every frame.
        IntermediatePoolTable<8> pool;

        const IntermediateShape eye{2064, 2208, 28};
        const IntermediateShape reticle{64, 64, 28};

        const IntermediatePoolSlot firstEye = pool.Acquire(eye);
        const bool coldCreate = firstEye.valid && firstEye.needsCreate && !firstEye.evicted.Valid();
        pool.Commit(firstEye.index, eye);

        const IntermediatePoolSlot firstReticle = pool.Acquire(reticle);
        const bool secondShapeGetsItsOwnSlot = firstReticle.valid && firstReticle.needsCreate &&
                                               !firstReticle.evicted.Valid() && firstReticle.index != firstEye.index;
        pool.Commit(firstReticle.index, reticle);

        // THE REGRESSION THIS LOCKS DOWN: alternating shapes must both be
        // resident. The old single intermediate reported needsCreate on every
        // one of these, at full render size.
        bool alternatingReuse = true;
        for (int frame = 0; frame < 4; ++frame)
        {
            const IntermediatePoolSlot e = pool.Acquire(eye);
            const IntermediatePoolSlot r = pool.Acquire(reticle);
            alternatingReuse = alternatingReuse && e.valid && !e.needsCreate && e.index == firstEye.index && r.valid &&
                               !r.needsCreate && r.index == firstReticle.index;
        }
        Check(coldCreate && secondShapeGetsItsOwnSlot && alternatingReuse,
            "The intermediate pool keeps one copy per shape, so alternating "
            "sources stop rebuilding a full-resolution texture every frame");

        // THE PROPERTY THAT MAKES A CAPACITY OF 32 SAFE: the pool is a ceiling,
        // not a preallocation. A big cap must hold only the shapes actually
        // asked for, so raising it cannot cost VRAM that is never requested.
        IntermediatePoolTable<32> ceiling;
        const bool emptyUntilAsked = ceiling.Size() == 0;
        const IntermediateShape onlyShape{2064, 2208, 28};
        for (int frame = 0; frame < 50; ++frame)
        {
            const IntermediatePoolSlot s = ceiling.Acquire(onlyShape);
            if (s.needsCreate)
                ceiling.Commit(s.index, onlyShape);
        }
        // Fifty frames of one shape occupy exactly one slot out of thirty-two,
        // and only the first frame created anything.
        const bool onlyWhatWasAskedFor = ceiling.Size() == 1 && ceiling.Stats().misses == 1 &&
                                         ceiling.Stats().hits == 49 && ceiling.Stats().evictions == 0;
        Check(emptyUntilAsked && onlyWhatWasAskedFor,
            "The intermediate pool's capacity is a ceiling, not a "
            "preallocation: 32 slots hold only the shapes actually requested");

        // A shape differing only in format is a different shape - reusing a
        // copy across formats would sample the wrong bits.
        const IntermediateShape sameSizeOtherFormat{2064, 2208, 87};
        const IntermediatePoolSlot recoloured = pool.Acquire(sameSizeOtherFormat);
        const bool formatIsPartOfIdentity = recoloured.needsCreate && recoloured.index != firstEye.index;
        Check(formatIsPartOfIdentity, "Format is part of an intermediate's identity, so a copy is never "
                                      "reused across formats");

        // Eviction at a small capacity reports the shape whose resource the
        // caller must release, so a slot is never overwritten while occupied.
        IntermediatePoolTable<2> tight;
        const IntermediateShape s1{100, 100, 1};
        const IntermediateShape s2{200, 200, 1};
        const IntermediateShape s3{300, 300, 1};
        const IntermediatePoolSlot t1 = tight.Acquire(s1);
        tight.Commit(t1.index, s1);
        const IntermediatePoolSlot t2 = tight.Acquire(s2);
        tight.Commit(t2.index, s2);
        (void)tight.Acquire(s1); // `s1` freshest, so `s2` is the victim
        const IntermediatePoolSlot t3 = tight.Acquire(s3);
        const bool evictsLru =
            t3.needsCreate && t3.evicted == s2 && t3.index == t2.index && tight.Stats().evictions == 1;
        tight.Commit(t3.index, s3);

        // A creation that fails must abandon its slot rather than leave a
        // shape claimed by a resource that does not exist.
        const IntermediatePoolSlot failing = tight.Acquire(IntermediateShape{400, 400, 1});
        tight.Abandon(failing.index);
        const bool abandonClears =
            !tight.Live(failing.index) && tight.Acquire(IntermediateShape{400, 400, 1}).needsCreate;

        // Degenerate shapes are refused outright rather than claiming a slot.
        IntermediatePoolTable<2> guards;
        const bool rejectsDegenerate = !guards.Acquire(IntermediateShape{0, 100, 1}).valid &&
                                       !guards.Acquire(IntermediateShape{100, 0, 1}).valid && guards.Size() == 0;

        Check(evictsLru && abandonClears && rejectsDegenerate,
            "The intermediate pool evicts least-recently-used, clears an "
            "abandoned slot, and refuses a degenerate shape");
    }

    CheckUnit(249);
    // Roomscale uses observed native travel, never teleports the body or
    // consumes a requested step before the engine has actually moved.
    for (float scale : {0.1f, 0.328084f, 1.0f})
        for (float yaw : {0.0f, 1.57079632679f, -1.57079632679f})
        {
            RoomscaleFollow follow;
            float body[3]{}, head[3]{0, 1.7f, 0}, reference[3]{0, 1.7f, 0};
            const float wx = std::cos(yaw), wy = std::sin(yaw);
            float mx = 0, my = 0;
            Check(follow.Update(7, 100, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my) && mx == 0 &&
                      my == 0,
                "roomscale starts without an unsolicited walk");
            head[2] = -0.2f;
            follow.Update(7, 111, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(my > 0 && std::fabs(mx) < 1e-6f && reference[2] == 0,
                "physical forward requests native movement without consuming blocked travel");
            follow.Update(7, 122, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(reference[2] == 0 && my > 0, "wall obstruction cannot falsely move tracking origin");
            body[0] = wx * 0.1f * scale;
            body[1] = wy * 0.1f * scale;
            follow.Update(7, 133, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(std::fabs(reference[2] + 0.1f) < 1e-5f && std::fabs(reference[1] - 1.7f) < 1e-5f,
                "native body travel consumes horizontal tracked lean once at every tested scale and heading");
            const float viewedForward = (body[0] * wx + body[1] * wy) / scale - (head[2] - reference[2]);
            Check(std::fabs(viewedForward - 0.2f) < 1e-5f,
                "body plus remaining lean equals the physical step, without double camera movement");
            head[0] = 0.15f;
            follow.Update(7, 144, true, true, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            follow.Update(7, 155, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(mx == 0 && my == 0, "manual locomotion pauses roomscale until native braking is quiet");
            head[0] += 0.1f;
            follow.Update(7, 166, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(mx == 0 && my == 0, "new physical step also waits for the manual quiet window");
            follow.Update(7, 316, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(mx > 0 && my > 0, "physical right step and retained forward debt both survive manual movement");
            const float before = reference[0];
            body[0] += 10 * scale;
            follow.Update(7, 327, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(
                mx == 0 && my == 0 && reference[0] == before, "teleport rebases demand without consuming false travel");
            head[0] += 2;
            follow.Update(7, 338, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(mx == 0 && my == 0, "tracking jump cannot become a long unattended walk");
            head[0] += 0.1f;
            follow.Update(8, 349, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(mx == 0 && my == 0, "new title generation cancels pending movement");
            head[0] += 0.1f;
            follow.Update(8, 360, false, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(mx == 0 && my == 0 && !follow.seeded, "disabled roomscale leaves existing tracking reference alone");
            follow.Update(8, 371, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            head[0] += 0.1f;
            follow.Update(8, 750, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(mx == 0 && my == 0, "stale camera resumes without replaying physical movement");
            reference[0] = head[0];
            reference[2] = head[2];
            follow.Update(8, 761, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my);
            Check(mx == 0 && my == 0, "manual recenter cancels prior roomscale demand");
            head[0] = std::numeric_limits<float>::quiet_NaN();
            Check(!follow.Update(8, 772, true, false, body, head, reference, 0, -1, wx, wy, scale, mx, my) && mx == 0 &&
                      my == 0,
                "nonfinite tracking cannot produce locomotion");
        }
    CheckUnit(251);
    Check(!Config{}.roomscale_movement, "roomscale is opt-in for existing and new configurations");

    CheckUnit(253);
    g_failures += RunRoomscaleInputTests();
    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core platform tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
