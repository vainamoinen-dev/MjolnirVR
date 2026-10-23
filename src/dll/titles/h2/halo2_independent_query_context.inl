// Per-thread context that records the Halo 2 native aim query for independent shots.
// Included once, by src/dll/titles/h2/halo2_observer_6dof.cpp; also compiled by
//   tests/halo2_independent_shots_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line (headers only).
#pragma once
// Published only after the existing local native acquisition finishes. TLS
// prevents calling the native query's shared sorting work on another thread.
struct Halo2IndependentQueryContext
{
    uint32_t unit = UINT32_MAX, generation = 0;
    uint64_t sampleMs = 0;
};
thread_local Halo2IndependentQueryContext g_halo2IndependentQueryContext;
void RecordHalo2IndependentQuery();
