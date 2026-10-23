# Reach "extremely grainy" in VR: causes and fix list (research, 2026-10-10)

Read-only. Nothing launched. Basis: source at cd179b7, live cfg and logs (HaloMCCVR.log.prev, 12:59-13:01 Reach session), web search (snippets only).
Tags: [S] source/log fact, [W] web claim, [I] inference, untested.

## 1. What the mod does to Reach [S]
- No Reach-specific AA, dither, grain, MSAA or render-scale code. Reach shares the universal eye image-quality chain (src/dll/vr.cpp BlitImageQuality; keys config.h:971-980).
- No engine resolution variable exists (docs/RESOLUTION-FSR-INVESTIGATION.md:193-200); the scene is rendered at the swapchain size the launcher forces.
- User cfg (halomccvr.cfg): aa_mode=4 (SMAA 1x + FXAA Strong; default 0), sharpness=0.30 (RCAS), upscale_filter=1 (bicubic a=-0.75), resolution_scale=1.00, upscaler=0 (DLSS off), bloom_enabled_reach=1, motion_blur=0.
- Reach session log: game renders 2912x2100; headset recommends 1440x1584 per eye; eye swapchain 2151x1750; submitted native-FOV rect ~67%x90% of the cover (~1950x1890 source px) -> 1440x1584. So Reach is DOWNSAMPLED ~1.35x here, not upscaled (an earlier 2496x2688 recommendation was a different headset/runtime setting). Grain is therefore not a low-resolution symptom.
- Reach-only already handled: rain (off), atmospheric fog bit 0x04 (off), screen-aligned patchy fog (suppressed per eye), motion-blur max (zeroed). Native SSAO/HDAO cannot be switched off: the isolation attempt blacked out the world (docs/CURRENT-STATE.md:3871, REACH-SIGNATURE-EVIDENCE.md:569).
- Reach's SSAO runs per owned eye on private downsampled depth/normal targets (REACH-SIGNATURE-EVIDENCE.md:584-616). [I] A half-res noisy AO term or dithered shadow filtering is a likely grain source, and H3/ODST are not the same engine path.

## 2. Known causes in MCC Reach [W]
- MCC Reach dropped the 360 version's temporal AA (Vice/Digital Foundry), so stochastic/dithered effects are no longer temporally averaged. [I] This fits "only Reach is grainy": the other five titles either never relied on TAA or have no stipple.
- Steam thread: grain is constant in Reach, most visible in the sky; disabling ReShade did not remove it; no setting found.
- A ReShade preset page notes film grain baked into MCC shaders that sharpening makes worse. [I] Our RCAS sharpness 0.30 and bicubic a=-0.75 (ringing filter) amplify exactly that.
- MCC PC has only Performance / Original / Enhanced presets plus AA/texture/shadow sliders; Enhanced adds fog/weather and more AA (gamewatcher). No console variables for grain/dither/SSAO are documented anywhere found.
- Engine.ini keys (r.FilmGrain etc.) are for Halo: Campaign Evolved (Unreal), not MCC. Do not use.
- Not found: any other VR mod or user fix specific to Reach grain. The upstream mod's notes recommend MCC FSR Off.

## 3. Ranked fix list (cheapest first, all F1/cfg unless noted)
1. sharpness 0.30 -> 0.00 (live). RCAS amplifies noise. 10-second A/B.
2. upscale_filter 1 -> 0 (live): linear averaging instead of a=-0.75 bicubic on a downsample; kills ringing on noise.
3. aa_mode 4 -> 0, then 1 (live). Cheap to rule out stacked-filter shimmer; FXAA/SMAA do not add grain but SMAA after noise can flicker.
4. Check MCC in-game Reach video settings (needs a QA launch): FSR/upscaling OFF (mod-invisible, breaks capture), try Original vs Enhanced preset, AA setting, shadow quality. Where the preset file lives was not found on this PC (no GameUserSettings.ini under %LOCALAPPDATA%\HaloMCC); do not edit blindly.
5. bloom_enabled_reach 0 (live): bloom lifts and shimmers grain on bright sky. Minor.
6. resolution_scale 1.00 -> ~1.3-1.5 (restart; headset-pending tiers). Finer noise averages out in the downsample. GPU cost; RTX 4070 headroom unverified.
7. upscaler = DLSS (dlss.cpp, Reach depth/jitter paths exist, martysl1 Reach-DLSS work audited in docs/COMMUNITY-REPORT-AUDIT-2026-09-23.md). Temporal accumulation is the real substitute for the lost TAA. Biggest likely win, but unproven on Reach; needs QA.
8. Mod-side fixes needing code: (a) mod-owned temporal accumulation / DLAA-style resolve on the eye; (b) bilateral blur of the SSAO term (hook at 0x002A13A0, owned-eye only); (c) find the dither/noise texture in haloreach.dll shaders (HREK tag shaders) and replace it with a flat one. Each needs RE first.

## 4. Gaps
- No capture of the grain exists; first step is a Reach screenshot per eye with items 1-3 toggled, to see whether noise is scene (SSAO/shadow/sky) or post (sharpen).
- HREK shader/rasterizer tags not searched for dither tables; only area_screen_effect tags found (weapon fx, not grain).
- Web claims are from snippets; pages not opened.
