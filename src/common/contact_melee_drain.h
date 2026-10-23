#pragma once
// Per-tick drain of both hands' contact melee packet queues, shared by the title runtimes.
// Used by dll/core/game.cpp, the Halo 2 observer and the contact melee tests.
//
// Shared per-tick drain of the two hands' contact packet queues (Halo 3, ODST,
// Halo 2, Halo 4, Reach). Each title keeps its own admission decision, its own
// native Backend and its own counters/haptics; this owns the common rule:
//   per hand, pop at most `maxPackets`; a packet that is refused (not admitted,
//   another generation or unit, from the future, or older than `maxAgeMs`)
//   resets that hand's swing tracking and is skipped; a fresh packet is run
//   through Hand::Process with this tick's swing speed and its result reported.
//   With `resetWhenRefused`, a hand is also reset after the drain whenever the
//   tick was not admitted (Halo 4 does not do this).
// Halo CE is not a client: its loop interleaves the world-contact worker and
// per-packet melee admission, with its own epoch/serial checks.
// Pure logic: no globals, no locks, no logging.
#include <cstdint>

namespace contact_melee
{
    struct DrainPolicy
    {
        unsigned maxPackets = 8;
        uint64_t maxAgeMs = 100;
        bool resetWhenRefused = true;
        int64_t rearmNs = 60'000'000; // least time between one hand's strikes
    };

    // Packet: the queue's element type. makeBackend(hand) returns the title's
    // Backend for this packet; onResult(hand, result) sees every Process result.
    template <class Packet, class Queue, class Hand, class MakeBackend, class OnResult>
    void DrainHands(Queue (&queues)[2], Hand (&hands)[2], bool admit, uint32_t generation, uint32_t unit, uint64_t now,
        float swingSpeed, const DrainPolicy& policy, MakeBackend makeBackend, OnResult onResult)
    {
        for (int hand = 0; hand < 2; ++hand)
        {
            Packet packet{};
            for (unsigned n = 0; n < policy.maxPackets && queues[hand].Pop(packet); ++n)
            {
                // Another module generation or body resets the hand;
                // a stale or future packet (a hitch) only restarts its swing tracking, so the
                // strike latch holds and one swing stays one melee.
                if (!admit || packet.generation != generation || packet.frame.unit != unit)
                {
                    hands[hand].Reset();
                    continue;
                }
                if (packet.publishedAtMs > now || now - packet.publishedAtMs > policy.maxAgeMs)
                {
                    hands[hand].ResetMotion();
                    continue;
                }
                auto backend = makeBackend(hand);
                onResult(hand, hands[hand].Process(packet.frame, swingSpeed, backend, policy.rearmNs));
            }
            if (!admit && policy.resetWhenRefused)
                hands[hand].Reset();
        }
    }
} // namespace contact_melee
