// Halo 4 VR turn: applies smooth and snap turn from the controller stick while controller aim is active.
// Smooth turn uses a sub-frame timebase because this runs several times per frame.
#pragma once
// Included once, by src/dll/titles/h4/halo4_camera_core.cpp; also compiled by
//   tests/vehicle_turn_tests.cpp, which supplies its own versions. Not a standalone header: it
//   uses what the host defines above its include line, namely Game_NoteSnapTurn,
//   Halo4ControllerAimActive, Halo4ReadVehicleInput, g_halo4Camera.
void Halo4ApplyVrTurn(const VrPadState& pad)
{
    if (!Halo4ControllerAimActive() || !pad.valid)
        return;
    // Same sub-frame timebase reasoning as Halo 3's ApplyVrTurn: this runs
    // several times per frame, and GetTickCount's ~15.6 ms granularity
    // turned smooth turn into a visible ~5 Hz stutter there.
    static LARGE_INTEGER freq{}, last{};
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = last.QuadPart == 0 ? 0.0f : (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
    last = now;
    if (dt > 0.1f)
        dt = 0.1f;

    const float x = pad.turnX; // stick right = turn right = yaw decreases
    static bool snapLatched = false;
    float reference = g_halo4Camera.gameYawReference.load(std::memory_order_relaxed);
    Halo4VehicleInputState seat{};
    const bool seated = g_config.vehicle_smooth_turn && Halo4ReadVehicleInput(seat) && seat.seated;
    if (UseSmoothVrTurn(g_config.turn_smooth, g_config.vehicle_smooth_turn, seated))
    {
        Halo3ConsumeSnapTurn(false, x, snapLatched);
        if (fabsf(x) > 0.15f)
        {
            reference = WrapPi(reference - x * (g_config.turn_smooth_deg_s / 57.2958f) * dt);
            g_halo4Camera.gameYawReference.store(reference, std::memory_order_relaxed);
            g_halo4Camera.vrTurns.fetch_add(1, std::memory_order_relaxed);
            VR_NoteSmoothTurnApplied(); // turn vignette follows applied yaw only
        }
    }
    else if (Halo3ConsumeSnapTurn(true, x, snapLatched))
    {
        Game_NoteSnapTurn(GameTitle::Halo4);
        reference = WrapPi(reference - (x > 0 ? 1.0f : -1.0f) * g_config.turn_snap_deg / 57.2958f);
        g_halo4Camera.gameYawReference.store(reference, std::memory_order_relaxed);
        g_halo4Camera.vrTurns.fetch_add(1, std::memory_order_relaxed);
    }
}
