// core_h3_odst_ce_tests.cpp: Halo 3 vehicle snapshots, legacy weapon bounds and melee responses, ODST offsets and CE admission.
// Units 5, 98, 115, 231, 238, 246 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_h3_odst_ce_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(5);
    {
        for (float offset : {0.0f, 0.17f, -0.17f, 1.0f, -1.0f})
            Check(OdstPlainPerspectiveScalars(0, 1.641310f, 1.345714f, offset, 0.0078125f, 10240.0f),
                "ODST report's ordinary camera admits authored observer offsets");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        Check(!OdstPlainPerspectiveScalars(0, 1.6f, 1.3f, nan, .01f, 100.f) &&
                  !OdstPlainPerspectiveScalars(0, 1.6f, 1.3f, inf, .01f, 100.f) &&
                  !OdstPlainPerspectiveScalars(1, 1.6f, 1.3f, .17f, .01f, 100.f) &&
                  !OdstPlainPerspectiveScalars(0, 0.f, 1.3f, .17f, .01f, 100.f) &&
                  !OdstPlainPerspectiveScalars(0, 1.6f, inf, .17f, .01f, 100.f) &&
                  !OdstPlainPerspectiveScalars(0, 1.6f, 1.3f, .17f, 0.f, 100.f) &&
                  !OdstPlainPerspectiveScalars(0, 1.6f, 1.3f, .17f, 100.f, 10.f),
            "ODST offset compatibility retains mode, finite, FOV and clip guards");
    }

    CheckUnit(98);
    {
        const TitleDescriptor* descriptor = TitleRegistry_Find(GameTitle::HaloCE);
        const bool admitted = descriptor && (descriptor->admissionCapabilities & TitleCapability_ControllerInput) != 0;
        Check(descriptor && (descriptor->capabilities & TitleCapability_Haptics) &&
                  !(descriptor->admissionCapabilities & TitleCapability_Haptics),
            "CE offers native rumble only through an armed runtime, in both graphics modes");
        Check(admitted && TitleRegistry_AllowsSharedControllerInput(GameTitle::HaloCE, false, false, true, admitted),
            "CE receives the existing virtual-controller transport without granting unimplemented aiming");
        Check(TitleRegistry_HookPlan(GameTitle::HaloCE) == TitleHookPlan::HaloCECameraCore &&
                  !(descriptor->capabilities &
                      (TitleCapability_ControllerAim | TitleCapability_ArmIk | TitleCapability_Hud)) &&
                  !TitleRegistry_AllowsGenericDrawDistance(GameTitle::HaloCE, true),
            "CE camera admission does not enable other engines' aiming, HUD, IK or generic native writes");
    }
    CheckUnit(115);
    {
        // Enclosed-cockpit default eye height (option c): defaults lift the eye, an
        // explicit per-seat trim wins, the legacy switch restores the universal trim.
        Config c{};
        c.vehicle_cockpit_eye_up = true; // the helpers are opt-in now (default off)
        const int h3Wraith = ConfigSeatTrimSlot(5, 0, false), odstWraith = ConfigOdstSeatTrimSlot(5, 0, false);
        Check(ConfigSeatCamUp(c, h3Wraith) == kCockpitEyeUpWraithM &&
                  ConfigOdstSeatCamUp(c, odstWraith) == kCockpitEyeUpWraithM,
            "H3 and ODST wraith drivers start with the eye raised");
        Check(ConfigSeatCamUp(c, ConfigSeatTrimSlot(2, 0, false)) == c.vehicle_cam_up_m,
            "other seats keep the universal up trim");
        Check(
            ConfigVehicleModelCam(c, GameTitle::Halo2, kHalo2WraithIdentity, 0, 1) == kCockpitEyeUpH2WraithM &&
                ConfigVehicleModelCam(c, GameTitle::Halo2, kHalo2ScorpionIdentity, 0, 1) == kCockpitEyeUpH2ScorpionM &&
                ConfigVehicleModelCam(c, GameTitle::Halo2, kHalo2WraithIdentity, 1, 1) ==
                    ConfigGameVehicleCam(c, GameTitle::Halo2, 1) &&
                ConfigVehicleModelCam(c, GameTitle::Halo2, kHalo2WraithIdentity, 0, 0) ==
                    ConfigGameVehicleCam(c, GameTitle::Halo2, 0),
            "H2 Scorpion/Wraith driver up is raised; other seats and axes are not");
        c.vehicle_cam_up_set[h3Wraith] = true;
        c.vehicle_cam_up_v[h3Wraith] = 0.7f;
        Check(ConfigSeatCamUp(c, h3Wraith) == 0.7f, "an explicit seat trim wins over the cockpit default");
        const int modelSlot = ConfigEnsureVehicleModelTrim(c, GameTitle::Halo2, kHalo2WraithIdentity, 0);
        c.vehicle_model_trims[modelSlot].set[1] = true;
        c.vehicle_model_trims[modelSlot].value[1] = 0.3f;
        Check(ConfigVehicleModelCam(c, GameTitle::Halo2, kHalo2WraithIdentity, 0, 1) == 0.3f,
            "and so does an explicit model trim");
        Config legacy{};
        legacy.vehicle_cockpit_eye_up = false;
        Check(ConfigSeatCamUp(legacy, h3Wraith) == legacy.vehicle_cam_up_m &&
                  ConfigVehicleModelCam(legacy, GameTitle::Halo2, kHalo2WraithIdentity, 0, 1) ==
                      ConfigGameVehicleCam(legacy, GameTitle::Halo2, 1),
            "with the switch off nothing changes");
    }
    CheckUnit(231);
    {
        for (auto layout : {LegacyBoundsLayout::Halo3, LegacyBoundsLayout::Odst, LegacyBoundsLayout::Reach})
        {
            uint8_t record[52]{};
            record[0] = 1;
            const size_t bytes = LegacyCompressionRecordBytes(layout);
            const float minimum[3]{-0.3f, -0.02f, -0.04f}, maximum[3]{0.8f, 0.02f, 0.05f};
            for (int axis = 0; axis < 3; ++axis)
            {
                memcpy(record + 4 + axis * 8, &minimum[axis], 4);
                memcpy(record + 8 + axis * 8, &maximum[axis], 4);
            }
            Halo4WeaponCollisionBounds bounds{};
            Check(LegacyDecodeCompressionBounds(layout, std::span(record, bytes), bounds) &&
                      bounds.minimum[0] == minimum[0] && bounds.maximum[2] == maximum[2],
                "independently sized legacy records retain authored bounds");
            const auto before = bounds;
            const float invalid = NAN;
            memcpy(record + 0x10, &invalid, 4);
            Check(!LegacyDecodeCompressionBounds(layout, std::span(record, bytes), bounds) &&
                      !memcmp(&bounds, &before, sizeof(bounds)),
                "invalid legacy data refuses atomically");
            Check(!LegacyDecodeCompressionBounds(layout, std::span(record, bytes - 1), bounds),
                "truncated legacy compression cannot publish a weapon");
            const auto shape = LegacyRuntimeWeaponShapeId(42, 37, before);
            Check(shape != LegacyRuntimeWeaponShapeId(43, 37, before) &&
                      shape != LegacyRuntimeWeaponShapeId(42, 38, before),
                "tag reuse and mapped-root changes reseed legacy melee");
        }
        int32_t body[37];
        for (int i = 0; i < 37; ++i)
            body[i] = i;
        Check(LegacyBodyPrefixFromRemap(body, 42, 41, 10, 26) == 37,
            "complete native body remap establishes appended-weapon boundary");
        const int32_t weapon[]{37, 38, -1, 40}, arm[]{10, 11}, camera[]{37, 41};
        Check(LegacyRemapIsAppendedWeapon(weapon, 37, 41, 42) && !LegacyRemapIsAppendedWeapon(body, 37, 41, 42) &&
                  !LegacyRemapIsAppendedWeapon(arm, 37, 41, 42) && !LegacyRemapIsAppendedWeapon(camera, 37, 41, 42),
            "unfamiliar held graph accepts optional nodes while excluding body and camera");
        body[36] = 35;
        Check(LegacyBodyPrefixFromRemap(body, 42, 41, 10, 26) < 0,
            "incomplete body remap cannot authorize arbitrary model bounds");
        Check(!LegacyRemapIsAppendedWeapon(weapon, 36, 41, 42) && !LegacyRemapIsAppendedWeapon(weapon, 37, 64, 65),
            "changed layout and oversized source stay within established contact guards");
    }
    CheckUnit(238);
    {
        // Halo 3 vehicle snapshot: generation keying must reject stale and
        // zero generations exactly like the Reach precedent, and the optional
        // type/seat refinements must read as unproven (-1) whenever absent,
        // out of range, or stale.
        constexpr uint32_t gen = 41;
        constexpr uint64_t onFoot = Halo3VehicleSnapshot(gen, Halo3VehicleState::OnFoot);
        constexpr uint64_t warthogDriver = Halo3VehicleSnapshot(gen, Halo3VehicleState::Vehicle, 1, 0);
        constexpr uint64_t turretSeat = Halo3VehicleSnapshot(gen, Halo3VehicleState::Vehicle, 5, 2);
        constexpr uint64_t typeUnproven = Halo3VehicleSnapshot(gen, Halo3VehicleState::Vehicle);
        Check(Halo3VehicleSnapshotState(onFoot, gen) == Halo3VehicleState::OnFoot &&
                  Halo3VehicleSnapshotState(warthogDriver, gen) == Halo3VehicleState::Vehicle &&
                  Halo3VehicleSnapshotState(warthogDriver, gen + 1) == Halo3VehicleState::Unknown &&
                  Halo3VehicleSnapshotState(warthogDriver, 0) == Halo3VehicleState::Unknown,
            "Halo 3 vehicle snapshot state honours the generation key");
        Check(Halo3VehicleSnapshotType(warthogDriver, gen) == 1 && Halo3VehicleSnapshotSeat(warthogDriver, gen) == 0 &&
                  Halo3VehicleSnapshotType(turretSeat, gen) == 5 && Halo3VehicleSnapshotSeat(turretSeat, gen) == 2 &&
                  Halo3VehicleSnapshotType(typeUnproven, gen) == -1 &&
                  Halo3VehicleSnapshotSeat(typeUnproven, gen) == -1 &&
                  Halo3VehicleSnapshotType(warthogDriver, gen + 1) == -1 &&
                  Halo3VehicleSnapshotSeat(warthogDriver, 0) == -1,
            "Halo 3 vehicle snapshot type/seat unpack and degrade to unproven");
        Check(Halo3VehicleSnapshotType(Halo3VehicleSnapshot(gen, Halo3VehicleState::Vehicle, kHalo3VehicleTypeMax + 1,
                                           kHalo3VehicleSeatMax + 1),
                  gen) == -1 &&
                  Halo3VehicleSnapshotSeat(Halo3VehicleSnapshot(gen, Halo3VehicleState::Vehicle,
                                               kHalo3VehicleTypeMax + 1, kHalo3VehicleSeatMax + 1),
                      gen) == -1 &&
                  Halo3VehicleSnapshotType(
                      Halo3VehicleSnapshot(gen, Halo3VehicleState::Vehicle, kHalo3VehicleTypeMax, kHalo3VehicleSeatMax),
                      gen) == kHalo3VehicleTypeMax &&
                  Halo3VehicleSnapshotSeat(
                      Halo3VehicleSnapshot(gen, Halo3VehicleState::Vehicle, kHalo3VehicleTypeMax, kHalo3VehicleSeatMax),
                      gen) == kHalo3VehicleSeatMax,
            "Halo 3 vehicle snapshot rejects out-of-range refinements only");

        // Debounce: the stable state flips only after `threshold` consecutive
        // differing samples, the edge fires exactly once, and a flapping
        // sample restarts the count without ever surfacing.
        Halo3VehicleDebounce debounce;
        bool edged = false;
        for (uint32_t i = 0; i < kHalo3VehicleDebounceFrames - 1; ++i)
            edged = debounce.Update(Halo3VehicleState::Vehicle, kHalo3VehicleDebounceFrames);
        Check(!edged && debounce.stable == Halo3VehicleState::Unknown,
            "Vehicle entry does not settle before the debounce threshold");
        Check(debounce.Update(Halo3VehicleState::Vehicle, kHalo3VehicleDebounceFrames) &&
                  debounce.stable == Halo3VehicleState::Vehicle,
            "Vehicle entry settles exactly at the debounce threshold");
        Check(!debounce.Update(Halo3VehicleState::Vehicle, kHalo3VehicleDebounceFrames),
            "A settled state does not re-fire the edge");
        for (uint32_t i = 0; i < kHalo3VehicleDebounceFrames - 2; ++i)
            debounce.Update(Halo3VehicleState::OnFoot, kHalo3VehicleDebounceFrames);
        debounce.Update(Halo3VehicleState::Vehicle, kHalo3VehicleDebounceFrames);
        Check(debounce.stable == Halo3VehicleState::Vehicle, "A flap back to Vehicle restarts the exit count unseen");
        for (uint32_t i = 0; i < kHalo3VehicleDebounceFrames - 1; ++i)
            edged = debounce.Update(Halo3VehicleState::OnFoot, kHalo3VehicleDebounceFrames);
        Check(!edged && debounce.Update(Halo3VehicleState::OnFoot, kHalo3VehicleDebounceFrames) &&
                  debounce.stable == Halo3VehicleState::OnFoot,
            "Vehicle exit settles only after a full fresh debounce run");

        // Pin the byte-proven Halo 3 identities exactly like the Reach
        // vehicle test pins its HREK-matched RVAs. These expected values are
        // cross-checks; the runtime binding is always the unique AOB match.
        Check(kHalo3UnitInVehicleNativeRva == 0x3A36F4 && kHalo3PlayerUnitGetterRva == 0xEE48C &&
                  kHalo3VehicleTypeAccessorRva == 0x396D84 && kHalo3EngineTlsIndexRva == 0xA39F9C &&
                  kHalo3TlsObjectTableOffset == 0x38 && kHalo3ObjectTableEntriesOffset == 0x48 &&
                  kHalo3ObjectEntryStride == 0x18 && kHalo3ObjectEntryDataOffset == 0x10 &&
                  kHalo3ObjectKindVehicle == 1 && kHalo3ObjectParentOffset == 0x10 &&
                  kHalo3UnitSeatWordOffset == 0x24E && kHalo3TlsObserverOffset == 0x578 &&
                  kHalo3ObserverStride == 0x3D0 && kHalo3ObserverPosOffset == 0x11C &&
                  kHalo3ObserverFwdOffset == 0x144 && kHalo3ObserverUpOffset == 0x150 && kHalo3VehicleTypeTurret == 5 &&
                  kHalo3VehicleTypeNoneAuthored == 0xB && kHalo3VehicleFlyingTypeMask == 0x94u,
            "Halo 3 vehicle identities pin the evidence-doc values");

        // Focus-candidate scan: a planted focus = pos + fwd*d must be found
        // exactly, and a degenerate window must return no candidate.
        float observerWindow[kHalo3ObserverCaptureFloats];
        for (size_t i = 0; i < kHalo3ObserverCaptureFloats; ++i)
            observerWindow[i] = 1000.0f + static_cast<float>(i) * 7.0f;
        constexpr size_t posIndex = (kHalo3ObserverPosOffset - kHalo3ObserverCaptureBase) / 4;
        constexpr size_t fwdIndex = (kHalo3ObserverFwdOffset - kHalo3ObserverCaptureBase) / 4;
        observerWindow[posIndex] = 10.0f;
        observerWindow[posIndex + 1] = 20.0f;
        observerWindow[posIndex + 2] = 30.0f;
        observerWindow[fwdIndex] = 0.0f;
        observerWindow[fwdIndex + 1] = 1.0f;
        observerWindow[fwdIndex + 2] = 0.0f;
        observerWindow[0] = 10.0f; // planted focus = pos + fwd * 2.5
        observerWindow[1] = 22.5f;
        observerWindow[2] = 30.0f;
        observerWindow[30] = 2.5f; // planted focus distance
        const Halo3FocusCandidate found =
            Halo3FindFocusCandidate(observerWindow, kHalo3ObserverCaptureFloats, posIndex, fwdIndex);
        Check(
            found.tripletIndex == 0 && found.distanceIndex == 30 && found.residual < 1.0e-4f && found.distance == 2.5f,
            "Observer focus scan recovers a planted focus/distance pair");
        Check(Halo3FindFocusCandidate(nullptr, kHalo3ObserverCaptureFloats, posIndex, fwdIndex).tripletIndex == -1 &&
                  Halo3FindFocusCandidate(observerWindow, 2, 0, 1).tripletIndex == -1,
            "Observer focus scan rejects degenerate windows");

        // Transform-probe analysis: unit-length triplets (orientation
        // basis signature) and the triplet nearest a reference point
        // (position signature) must be found exactly and never invented.
        float xformWindow[kHalo3ParentCaptureFloats];
        for (size_t i = 0; i < kHalo3ParentCaptureFloats; ++i)
            xformWindow[i] = 500.0f + static_cast<float>(i);
        xformWindow[8] = 0.0f;
        xformWindow[9] = 0.6f; // unit triplet A
        xformWindow[10] = 0.8f;
        xformWindow[20] = 1.0f;
        xformWindow[21] = 0.0f; // unit triplet B
        xformWindow[22] = 0.0f;
        xformWindow[40] = 101.0f;
        xformWindow[41] = 202.0f; // position
        xformWindow[42] = 303.5f;
        const float xformRef[3] = {100.0f, 202.0f, 303.0f};
        const Halo3UnitTripletScan unitScan = Halo3FindUnitTriplets(xformWindow, kHalo3ParentCaptureFloats, 0.01f);
        bool sawA = false, sawB = false;
        for (int i = 0; i < unitScan.count && i < 12; ++i)
        {
            if (unitScan.indices[i] == 8)
                sawA = true;
            if (unitScan.indices[i] == 20)
                sawB = true;
        }
        const Halo3NearestTriplet nearest = Halo3FindNearestTriplet(xformWindow, kHalo3ParentCaptureFloats, xformRef);
        Check(sawA && sawB && nearest.index == 40 && nearest.distance < 1.5f &&
                  Halo3FindUnitTriplets(nullptr, 0, 0.01f).count == 0 &&
                  Halo3FindNearestTriplet(nullptr, 0, xformRef).index == -1,
            "Transform probe scans find planted basis and position triplets");

        // Identity resolution: physics type alone is a CLASS, so the
        // definition fields an in-process log confirmed must separate the
        // cousins, and anything unrecognised must resolve to Unknown.
        {
            Halo3DefinitionFields hogDef;
            hogDef.physicsType = 1;
            hogDef.jeepValid = true;
            hogDef.engineMoment = 2000.0f; // logged, def=05F9
            Halo3DefinitionFields gooseDef;
            gooseDef.physicsType = 1;
            gooseDef.jeepValid = true;
            gooseDef.engineMoment = 650.0f; // logged, def=0517
            Halo3DefinitionFields oddJeep;
            oddJeep.physicsType = 1;
            oddJeep.jeepValid = true;
            oddJeep.engineMoment = 1300.0f; // neither -> stock
            Halo3DefinitionFields blindJeep;
            blindJeep.physicsType = 1; // block unreadable
            Halo3DefinitionFields ghostDef;
            ghostDef.physicsType = 3;
            ghostDef.scoutValid = true;
            ghostDef.specificType = 1;
            Halo3DefinitionFields wraithDef;
            wraithDef.physicsType = 3;
            wraithDef.scoutValid = true;
            wraithDef.specificType = 3;
            Halo3DefinitionFields maulerDef;
            maulerDef.physicsType = 3;
            maulerDef.scoutValid = true;
            maulerDef.specificType = 4;
            Halo3DefinitionFields scoutOdd;
            scoutOdd.physicsType = 3;
            scoutOdd.scoutValid = true;
            scoutOdd.specificType = 7;
            Halo3DefinitionFields turretDef;
            turretDef.physicsType = 5; // never a root identity
            Halo3DefinitionFields tankDef;
            tankDef.physicsType = 0;
            Halo3DefinitionFields choppDef;
            choppDef.physicsType = 8;

            const bool cousinsSplit = Halo3ResolveVehicleId(hogDef) == Halo3VehicleId::Warthog &&
                                      Halo3ResolveVehicleId(gooseDef) == Halo3VehicleId::Mongoose &&
                                      Halo3ResolveVehicleId(ghostDef) == Halo3VehicleId::Ghost &&
                                      Halo3ResolveVehicleId(wraithDef) == Halo3VehicleId::Wraith &&
                                      Halo3ResolveVehicleId(maulerDef) == Halo3VehicleId::Mauler &&
                                      Halo3ResolveVehicleId(tankDef) == Halo3VehicleId::Scorpion &&
                                      Halo3ResolveVehicleId(choppDef) == Halo3VehicleId::Chopper;
            const bool unknownsStayUnknown = Halo3ResolveVehicleId(oddJeep) == Halo3VehicleId::Unknown &&
                                             Halo3ResolveVehicleId(blindJeep) == Halo3VehicleId::Unknown &&
                                             Halo3ResolveVehicleId(scoutOdd) == Halo3VehicleId::Unknown &&
                                             Halo3ResolveVehicleId(turretDef) == Halo3VehicleId::Unknown &&
                                             Halo3ResolveVehicleId(Halo3DefinitionFields{}) == Halo3VehicleId::Unknown;

            // The two authored jeep constants must not be within tolerance of
            // each other, or the resolver would be a coin toss.
            const bool constantsSeparate =
                std::fabs(kHalo3WarthogEngineMoment - kHalo3MongooseEngineMoment) > 2.0f * kHalo3EngineMomentTolerance;

            // Seat lookup keys on identity, not type: a mongoose must never
            // receive the warthog point, mounted gunners come from the
            // carrier's identity, and Unknown never resolves to anything.
            const Halo3SeatPoint* hogDriver = Halo3FindSeatPoint(Halo3VehicleId::Warthog, 0, false);
            const Halo3SeatPoint* hogGunner = Halo3FindSeatPoint(Halo3VehicleId::Warthog, 0, true);
            const Halo3SeatPoint* gooseDriver = Halo3FindSeatPoint(Halo3VehicleId::Mongoose, 0, false);
            const Halo3SeatPoint* hogPassenger = Halo3FindSeatPoint(Halo3VehicleId::Warthog, 1, false);
            const bool lookupOk = hogDriver && hogDriver->y > 0.1f && !hogDriver->carrierFrame && hogPassenger &&
                                  hogPassenger->y < -0.1f && gooseDriver && gooseDriver != hogDriver &&
                                  std::fabs(gooseDriver->z - hogDriver->z) > 0.1f && hogGunner &&
                                  hogGunner->carrierFrame && hogGunner != hogDriver &&
                                  // the gunner point is the same key but a different frame
                                  Halo3FindSeatPoint(Halo3VehicleId::Mongoose, 0, true) == nullptr &&
                                  Halo3FindSeatPoint(Halo3VehicleId::Unknown, 0, false) == nullptr &&
                                  Halo3FindSeatPoint(Halo3VehicleId::Ghost, 1, false) == nullptr &&
                                  Halo3FindSeatPoint(Halo3VehicleId::Hornet, 2, false) != nullptr &&
                                  Halo3FindSeatPoint(Halo3VehicleId::Banshee, 3, false) == nullptr;

            Check(cousinsSplit && unknownsStayUnknown && constantsSeparate && lookupOk,
                "Vehicle identity separates same-type cousins and an "
                "unidentified vehicle resolves to no point at all");
        }

        // ODST's own identity rules. ODST ships the same authored vehicles
        // as Halo 3, so the shared discriminators must behave identically —
        // but the shade is new, and it collides with the walk-up turrets
        // because neither authors a physics block. Only the seat's own driver
        // bit plus the engine's in-vehicle answer separate them.
        {
            OdstDefinitionFields hog;
            hog.physicsType = 1;
            hog.jeepValid = true;
            hog.engineMoment = kOdstWarthogEngineMoment;
            OdstDefinitionFields goose;
            goose.physicsType = 1;
            goose.jeepValid = true;
            goose.engineMoment = kOdstMongooseEngineMoment;
            OdstDefinitionFields ghost;
            ghost.physicsType = 3;
            ghost.scoutValid = true;
            ghost.specificType = 1;
            OdstDefinitionFields wraith;
            wraith.physicsType = 3;
            wraith.scoutValid = true;
            wraith.specificType = 3;
            const bool sharedVehiclesResolve = OdstResolveVehicleId(hog) == OdstVehicleId::Warthog &&
                                               OdstResolveVehicleId(goose) == OdstVehicleId::Mongoose &&
                                               OdstResolveVehicleId(ghost) == OdstVehicleId::Ghost &&
                                               OdstResolveVehicleId(wraith) == OdstVehicleId::Wraith;

            // A shade: no physics block authored, seat 0 is a driver seat, and
            // the engine reports the occupant is in a vehicle.
            OdstDefinitionFields shade;
            shade.physicsType = kOdstPhysicsTypeNoneAuthored;
            shade.seatFlagsValid = true;
            shade.seat0Flags =
                kOdstSeatDriverBit | kOdstSeatGunnerBit | kOdstSeatThirdPersonCameraBit; // the tag's authored 0x1C
            shade.inVehicle = true;
            // A walk-up machinegun turret: gunner seat, no driver bit.
            OdstDefinitionFields turret;
            turret.physicsType = kOdstPhysicsTypeNoneAuthored;
            turret.seatFlagsValid = true;
            turret.seat0Flags = kOdstSeatGunnerBit | kOdstSeatThirdPersonCameraBit;
            turret.inVehicle = false;
            // Same shape as the shade but the engine says it is not a
            // vehicle. This was asserted as "refuse to guess" -> Unknown, and
            // an ODST probe proved it is real shipped content: the
            // Covenant walk-up gun, `native=0 type=5 seats=1
            // seat0Flags=0x800C`. Refusing left it with no authored seat point
            // and put the camera at the vehicle root - in the ground. A turret
            // the engine does not call a vehicle is a walk-up emplacement.
            OdstDefinitionFields ambiguous = shade;
            ambiguous.inVehicle = false;
            // No seat evidence at all: refuse to guess.
            OdstDefinitionFields blind = shade;
            blind.seatFlagsValid = false;
            // Every real turret line from the ODST probe logs,
            // so a change to this switch can never again move a family that
            // was not checked. An earlier rule was rejected for exactly that.
            //
            // The Warthog's own mounted gun: in a vehicle, two seats, seat 0 a
            // GUNNER seat. It must keep resolving as it always has - an earlier rule swept
            // this into Shade and the Warthog turrets stopped working.
            OdstDefinitionFields warthogMountedGun;
            warthogMountedGun.physicsType = kOdstPhysicsTypeTurret;
            warthogMountedGun.seatFlagsValid = true;
            warthogMountedGun.seat0Flags = 0x101188u; // driver=0 gunner=1
            warthogMountedGun.inVehicle = true;
            // The Covenant walk-up gun: NOT a vehicle, one seat, and seat 0
            // carries the driver bit - which used to reject it into Unknown,
            // leaving it with no authored seat point at all.
            OdstDefinitionFields covenantWalkUp;
            covenantWalkUp.physicsType = kOdstPhysicsTypeTurret;
            covenantWalkUp.seatFlagsValid = true;
            covenantWalkUp.seat0Flags = 0x800Cu; // driver=1 gunner=1
            covenantWalkUp.inVehicle = false;
            const bool probedTurretsResolve =
                OdstResolveVehicleId(warthogMountedGun) == OdstVehicleId::StationaryTurret &&
                OdstResolveVehicleId(covenantWalkUp) == OdstVehicleId::StationaryTurret &&
                OdstFindSeatPoint(OdstVehicleId::StationaryTurret, 0, false) != nullptr;
            const bool shadeSplitsFromTurret = probedTurretsResolve &&
                                               OdstResolveVehicleId(shade) == OdstVehicleId::Shade &&
                                               OdstResolveVehicleId(turret) == OdstVehicleId::StationaryTurret &&
                                               OdstResolveVehicleId(ambiguous) == OdstVehicleId::StationaryTurret &&
                                               OdstResolveVehicleId(blind) == OdstVehicleId::Unknown &&
                                               OdstResolveVehicleId(OdstDefinitionFields{}) == OdstVehicleId::Unknown;

            // ODST's engine offsets must never coincide with Halo 3's, or a
            // mis-bound title would read plausible garbage instead of failing.
            const bool offsetsAreTitleSpecific =
                kOdstUnitSeatWordOffset != kHalo3UnitSeatWordOffset &&
                kOdstTlsObjectTableOffset != kHalo3TlsObjectTableOffset &&
                // Halo 3's node-bank cluster is 0x136/0x138 (game.cpp, not
                // exported here); ODST's is the same cluster shifted -0xC.
                kOdstObjectNodeBankSizeOffset == 0x12A && kOdstObjectNodeBankRelOffset == 0x12C &&
                // ...while the parts that genuinely are shared stay shared.
                kOdstObjectTableEntriesOffset == kHalo3ObjectTableEntriesOffset &&
                kOdstObjectEntryStride == kHalo3ObjectEntryStride && kOdstNodeMatrixStride == sizeof(Halo3Matrix4x3);

            // The node bank is only trusted when its byte size is a whole
            // number of matrices AND that count matches the render model.
            OdstProbeVehicleRecord good{};
            good.nodeBankByteSize = 16 * 0x34;
            good.tagNodeCount = 16;
            OdstProbeVehicleRecord ragged = good;
            ragged.nodeBankByteSize = 16 * 0x34 + 3;
            OdstProbeVehicleRecord mismatched = good;
            mismatched.tagNodeCount = 12;
            OdstProbeVehicleRecord empty{};
            const bool coherenceGateHolds = OdstNodeBankIsCoherent(good) && !OdstNodeBankIsCoherent(ragged) &&
                                            !OdstNodeBankIsCoherent(mismatched) && !OdstNodeBankIsCoherent(empty);

            const bool seatControlHolds =
                OdstSeatWordMeansUnseated(-1) && !OdstSeatWordMeansUnseated(0) && !OdstSeatWordMeansUnseated(3);

            // The object-table walk must refuse anything that is not provably
            // this build's "object" data array, because a wrong TLS slot would
            // otherwise hand it an arbitrary pointer to iterate.
            OdstDataArrayHeaderView table{};
            table.nameIsObject = true;
            table.signature = kOdstDataArraySignature;
            table.maximumCount = kOdstObjectTableCapacity;
            table.elementSize = kOdstObjectEntryStride;
            table.firstUnallocated = 400;
            table.valid = 1;
            OdstDataArrayHeaderView wrongName = table;
            wrongName.nameIsObject = false;
            OdstDataArrayHeaderView wrongMagic = table;
            wrongMagic.signature = 0x64407441;
            OdstDataArrayHeaderView wrongStride = table;
            wrongStride.elementSize = 0x1C;
            OdstDataArrayHeaderView notConnected = table;
            notConnected.valid = 0;
            OdstDataArrayHeaderView overrun = table;
            overrun.firstUnallocated = kOdstObjectTableCapacity + 1;
            const bool tableGateHolds =
                OdstObjectTableIsWalkable(table) && !OdstObjectTableIsWalkable(wrongName) &&
                !OdstObjectTableIsWalkable(wrongMagic) && !OdstObjectTableIsWalkable(wrongStride) &&
                !OdstObjectTableIsWalkable(notConnected) && !OdstObjectTableIsWalkable(overrun) &&
                !OdstObjectTableIsWalkable(OdstDataArrayHeaderView{}) &&
                // Only a zero identifier marks a free slot; 0xFFFF is a live
                // identifier in this engine, not a sentinel.
                !OdstObjectEntryIsLive(0) && OdstObjectEntryIsLive(0x8000) && OdstObjectEntryIsLive(0xFFFF);

            // ODST's config bank must be able to key every seat its tags
            // actually expose, which is exactly why it is wider than Halo 3's:
            // the Scorpion's four riders are player seats in ODST, and the
            // Shade exists at all. A slot collision here would put one seat's
            // trim on another.
            const int shadeSlot = ConfigOdstSeatTrimSlot(static_cast<int>(OdstVehicleId::Shade), 0, false);
            const int scorpionRider4 = ConfigOdstSeatTrimSlot(static_cast<int>(OdstVehicleId::Scorpion), 4, false);
            const int scorpionGunner = ConfigOdstSeatTrimSlot(static_cast<int>(OdstVehicleId::Scorpion), 0, true);
            const int warthogDriverInner = ConfigOdstSeatTrimSlot(static_cast<int>(OdstVehicleId::Warthog), 0, false);
            bool slotsUnique = shadeSlot >= 0 && scorpionRider4 >= 0 && scorpionGunner >= 0 &&
                               warthogDriverInner >= 0 && shadeSlot < kOdstVehicleTrimSlots &&
                               scorpionRider4 != scorpionGunner && scorpionRider4 != warthogDriverInner &&
                               shadeSlot != scorpionGunner;
            // Every seat ODST actually authors a point for must be keyable.
            for (const OdstSeatPoint& p : kOdstSeatPoints)
            {
                const int slot = ConfigOdstSeatTrimSlot(static_cast<int>(p.vehicle), p.seatIndex, p.carrierFrame);
                slotsUnique = slotsUnique && slot >= 0 && slot < kOdstVehicleTrimSlots;
            }
            // Halo 3's narrower table genuinely cannot key the ODST-only
            // seats -- which is the whole reason ODST has its own bank.
            const bool halo3CannotKeyOdstSeats =
                ConfigSeatTrimSlot(static_cast<int>(OdstVehicleId::Scorpion), 4, false) < 0 &&
                ConfigSeatTrimSlot(static_cast<int>(OdstVehicleId::Shade), 0, false) < 0;

            // An unset ODST seat follows the universal trim; a set one wins.
            Config c;
            const bool odstFallbackHolds = ConfigOdstSeatCamForward(c, shadeSlot) == c.vehicle_cam_forward_m &&
                                           ConfigOdstSeatCamUp(c, scorpionRider4) == c.vehicle_cam_up_m;
            c.odst_vehicle_cam_forward_v[shadeSlot] = 0.42f;
            c.odst_vehicle_cam_forward_set[shadeSlot] = true;
            const bool odstOverrideWins =
                ConfigOdstSeatCamForward(c, shadeSlot) == 0.42f &&
                // ...and it must not have disturbed the Halo 3 bank.
                ConfigSeatCamForward(c, ConfigSeatTrimSlot(static_cast<int>(Halo3VehicleId::Warthog), 0, false)) !=
                    0.42f;
            // The shipped ODST defaults must land on ODST slots, seeded from
            // the accepted Halo 3 tuning for the seats that diffed identical.
            const bool odstShippedSeeded =
                c.odst_vehicle_cam_forward_set[warthogDriverInner] &&
                c.odst_vehicle_cam_forward_v[warthogDriverInner] ==
                    c.vehicle_cam_forward_v[ConfigSeatTrimSlot(static_cast<int>(Halo3VehicleId::Warthog), 0, false)];

            Check(slotsUnique && halo3CannotKeyOdstSeats && odstFallbackHolds && odstOverrideWins && odstShippedSeeded,
                "The ODST trim bank keys every seat ODST authors, including "
                "the seats Halo 3's table cannot express, and stays "
                "independent of the Halo 3 bank");

            // Follow eligibility. The Shade is the ODST-specific rule: it
            // has a seat point, but its own body is what its aim turns, so
            // following it would cancel the closed loop's feedback exactly the
            // way a walk-up turret would.
            const bool shadeNeverFollows = OdstFindSeatPoint(OdstVehicleId::Shade, 0, false) != nullptr &&
                                           !OdstSeatFollowsHull(OdstVehicleId::Shade, 0, false) &&
                                           !OdstSeatFollowsPitch(OdstVehicleId::Shade, 0, false) &&
                                           !OdstSeatIsDriver(OdstVehicleId::Shade, 0, false) &&
                                           !OdstSeatAuthorsSteering(OdstVehicleId::Shade, 0, false, true);
            // Aircraft follow yaw but never pitch; ground vehicles do both.
            const bool aircraftStayYawOnly = OdstSeatFollowsHull(OdstVehicleId::Banshee, 0, false) &&
                                             !OdstSeatFollowsPitch(OdstVehicleId::Banshee, 0, false) &&
                                             OdstSeatFollowsHull(OdstVehicleId::Hornet, 0, false) &&
                                             !OdstSeatFollowsPitch(OdstVehicleId::Hornet, 0, false) &&
                                             OdstSeatFollowsPitch(OdstVehicleId::Warthog, 0, false) &&
                                             OdstSeatFollowsPitch(OdstVehicleId::Scorpion, 0, false);
            // The Scorpion and Wraith aim a turret independently of the hull,
            // so nothing may author their steering; aircraft are look-steered
            // but take the flight stick, not a wheel.
            const bool steeringFamiliesHold =
                !OdstVehicleIsLookSteered(OdstVehicleId::Scorpion) &&
                !OdstVehicleIsLookSteered(OdstVehicleId::Wraith) && OdstVehicleIsLookSteered(OdstVehicleId::Warthog) &&
                OdstVehicleUsesWheel(OdstVehicleId::Warthog) && !OdstVehicleUsesWheel(OdstVehicleId::Banshee) &&
                !OdstVehicleUsesWheel(OdstVehicleId::Hornet) &&
                // The follow toggle is the master switch for steering author.
                OdstSeatAuthorsSteering(OdstVehicleId::Warthog, 0, false, true) &&
                !OdstSeatAuthorsSteering(OdstVehicleId::Warthog, 0, false, false) &&
                // A passenger never authors steering, only the driver does.
                !OdstSeatAuthorsSteering(OdstVehicleId::Warthog, 1, false, true) &&
                // Walk-up turrets are excluded everywhere.
                !OdstSeatFollowsHull(OdstVehicleId::StationaryTurret, 0, false);

            // The seat flag that decides whose eye the shots leave. Only a
            // seat that lets the occupant use their own weapon may have its
            // aim re-origined onto the engine's eye; a driver or mounted
            // gunner fires a vehicle barrel.
            const bool personalWeaponSeatSplit =
                // ODST warthog passenger, flags 0x1070 (from its own tag)
                (0x1070u & kOdstSeatAllowsWeaponsBit) != 0 &&
                // ODST warthog driver, flags 0x40014
                (0x40014u & kOdstSeatAllowsWeaponsBit) == 0 &&
                // the machinegun turret gunner, flags 0x5500018
                (0x5500018u & kOdstSeatAllowsWeaponsBit) == 0 &&
                // and the bit must not collide with the ones already keyed
                kOdstSeatAllowsWeaponsBit != kOdstSeatDriverBit && kOdstSeatAllowsWeaponsBit != kOdstSeatGunnerBit &&
                kOdstSeatAllowsWeaponsBit != kOdstSeatThirdPersonCameraBit;

            Check(personalWeaponSeatSplit, "The allows-weapons seat flag separates a seat whose shots "
                                           "leave the occupant's own eye from a driver or turret gunner "
                                           "firing the vehicle's weapon");

            Check(shadeNeverFollows && aircraftStayYawOnly && steeringFamiliesHold,
                "ODST view-follow keeps the Shade and walk-up turrets out, "
                "holds aircraft to yaw-only, and authors steering for exactly "
                "the look-steered drivers");

            Check(sharedVehiclesResolve && shadeSplitsFromTurret && offsetsAreTitleSpecific && coherenceGateHolds &&
                      seatControlHolds && tableGateHolds,
                "ODST vehicle identity separates the shade from a walk-up "
                "turret, refuses to guess without seat evidence, keeps every "
                "engine offset title-specific, and only walks an object table "
                "that proves its own name and signature");
        }

        // Frame composition: a mounted turret's parent-relative frame must
        // compose through the carrier. Carrier at (10, 20, -5) yawed 90° left
        // (fwd = +Y world), turret mounted 0.5 behind carrier origin, 0.02 up,
        // facing carrier-forward: world origin = carrier + fwd*(-0.5) + up*0.02.
        {
            Halo3Frame carrier{};
            carrier.pos[0] = 10.0f;
            carrier.pos[1] = 20.0f;
            carrier.pos[2] = -5.0f;
            carrier.fwd[1] = 1.0f; // facing world +Y
            carrier.up[2] = 1.0f;
            Halo3Frame local{};
            local.pos[0] = -0.5f;
            local.pos[2] = 0.02f;
            local.fwd[0] = 1.0f;
            local.up[2] = 1.0f;
            const Halo3Frame world = Halo3ComposeFrame(carrier, local);
            // carrier left = up × fwd = (-1, 0, 0); local x rides carrier fwd.
            const bool posOk = std::fabs(world.pos[0] - 10.0f) < 1e-4f && std::fabs(world.pos[1] - 19.5f) < 1e-4f &&
                               std::fabs(world.pos[2] - (-4.98f)) < 1e-4f;
            const bool axesOk = std::fabs(world.fwd[1] - 1.0f) < 1e-4f && std::fabs(world.up[2] - 1.0f) < 1e-4f &&
                                Halo3FrameOrthonormal(world);
            // Identity carrier: composition must be exact pass-through.
            Halo3Frame ident{};
            ident.fwd[0] = 1.0f;
            ident.up[2] = 1.0f;
            const Halo3Frame same = Halo3ComposeFrame(ident, local);
            const bool identOk =
                std::fabs(same.pos[0] - local.pos[0]) < 1e-6f && std::fabs(same.pos[2] - local.pos[2]) < 1e-6f;
            // Sanity gate: the sky failure (frame near the map origin,
            // bounds at the real vehicle) must be rejected; a frame origin
            // inside the bounding sphere must pass; junk radius rejects.
            const float boundsCenter[3] = {9.9f, 12.1f, -21.4f};
            const float skyPos[3] = {-0.5f, 0.0f, 0.019f};
            Halo3Frame degenerate = local;
            degenerate.up[2] = 0.0f; // zero-length up
            Check(posOk && axesOk && identOk && !Halo3FrameOrthonormal(degenerate) &&
                      !Halo3AnchorWithinBounds(skyPos, boundsCenter, 0.8f, 2.0f) &&
                      Halo3AnchorWithinBounds(world.pos, world.pos, 0.8f, 2.0f) &&
                      !Halo3AnchorWithinBounds(skyPos, boundsCenter, 1e9f, 2.0f),
                "Frame composition carries mounted turrets and the sanity "
                "gate rejects wrong-frame anchors");
        }

        // Native parenting: calibration must preserve the exact Blender
        // point, while every later world-space movement comes from the native
        // camera even when the sampled object frame does not move at all.
        {
            Halo3Frame frame{};
            frame.pos[0] = 10.0f;
            frame.pos[1] = -3.0f;
            frame.pos[2] = 2.0f;
            frame.fwd[1] = 1.0f;
            frame.up[2] = 1.0f;
            const float nativeLocal[3] = {0.40f, -0.10f, 0.50f};
            const float authoredLocal[3] = {0.62f, 0.18f, 0.81f};
            float nativeOffset[3];
            Halo3FrameRotate(frame, nativeLocal, nativeOffset);
            float nativeWorld[3] = {
                frame.pos[0] + nativeOffset[0], frame.pos[1] + nativeOffset[1], frame.pos[2] + nativeOffset[2]};
            float recoveredLocal[3];
            const float relative[3] = {
                nativeWorld[0] - frame.pos[0], nativeWorld[1] - frame.pos[1], nativeWorld[2] - frame.pos[2]};
            Halo3FrameUnrotate(frame, relative, recoveredLocal);
            float anchor[3];
            Halo3NativeParentedAnchor(frame, nativeWorld, recoveredLocal, authoredLocal, anchor);
            float authoredOffset[3];
            Halo3FrameRotate(frame, authoredLocal, authoredOffset);
            const bool placementExact = std::fabs(anchor[0] - (frame.pos[0] + authoredOffset[0])) < 1e-5f &&
                                        std::fabs(anchor[1] - (frame.pos[1] + authoredOffset[1])) < 1e-5f &&
                                        std::fabs(anchor[2] - (frame.pos[2] + authoredOffset[2])) < 1e-5f;

            const float nativeDelta[3] = {1.25f, -0.75f, 0.33f};
            const float movedNative[3] = {
                nativeWorld[0] + nativeDelta[0], nativeWorld[1] + nativeDelta[1], nativeWorld[2] + nativeDelta[2]};
            float movedAnchor[3];
            Halo3NativeParentedAnchor(frame, movedNative, recoveredLocal, authoredLocal, movedAnchor);
            const bool nativeOwnsMotion = std::fabs((movedAnchor[0] - anchor[0]) - nativeDelta[0]) < 1e-5f &&
                                          std::fabs((movedAnchor[1] - anchor[1]) - nativeDelta[1]) < 1e-5f &&
                                          std::fabs((movedAnchor[2] - anchor[2]) - nativeDelta[2]) < 1e-5f;
            Check(placementExact && nativeOwnsMotion, "Native seat motion passes through exactly while the Blender "
                                                      "point remains the calibrated camera placement");
        }

        // Fingerprint: the same physics type covers several vehicles, so
        // the authored point is keyed on the tag's node-0 offset recovered
        // from data already sampled. It must be independent of where the
        // vehicle is and which way it faces, and ambiguity must never pick.
        {
            // Warthog-like: node 0 sits 0.30 up and 0.05 forward of the origin.
            const float node0[3] = {0.05f, 0.0f, 0.30f};
            // Same vehicle parked at the origin facing +X...
            Halo3Frame a{};
            a.fwd[0] = 1.0f;
            a.up[2] = 1.0f;
            float centerA[3] = {0.05f, 0.0f, 0.30f};
            float fpA[3] = {0.0f, 0.0f, 0.0f};
            const bool okA = Halo3ComputeFingerprint(a, centerA, fpA);
            // ...and the same vehicle far away, yawed 90 deg left (fwd = +Y).
            Halo3Frame b{};
            b.pos[0] = 120.5f;
            b.pos[1] = -44.25f;
            b.pos[2] = 8.0f;
            b.fwd[1] = 1.0f;
            b.up[2] = 1.0f;
            // world center = pos + fwd*0.05 + up*0.30 (left is unused here)
            float centerB[3] = {120.5f, -44.25f + 0.05f, 8.0f + 0.30f};
            float fpB[3] = {0.0f, 0.0f, 0.0f};
            const bool okB = Halo3ComputeFingerprint(b, centerB, fpB);
            const bool invariant = okA && okB && Halo3FingerprintDistance(fpA, node0) < 1e-4f &&
                                   Halo3FingerprintDistance(fpB, node0) < 1e-4f;

            // A degenerate frame must refuse rather than emit junk.
            Halo3Frame bad = a;
            bad.up[2] = 0.0f;
            float ignored[3];
            const bool refuses = !Halo3ComputeFingerprint(bad, centerA, ignored);

            // Uniqueness: close winner with a distant runner-up identifies;
            // two close candidates are ambiguous and must produce no point;
            // a lone candidate that is simply too far also fails.
            const bool picks = Halo3FingerprintIsUnique(0.02f, 0.55f);
            const bool ambiguous = !Halo3FingerprintIsUnique(0.02f, 0.08f);
            const bool tooFar = !Halo3FingerprintIsUnique(0.40f, 9.0f);
            const bool loneOk = Halo3FingerprintIsUnique(0.01f, std::numeric_limits<float>::infinity());

            Check(invariant && refuses && picks && ambiguous && tooFar && loneOk,
                "Bounding-center fingerprint is pose-invariant and refuses to "
                "identify a vehicle when two candidates are both plausible");
        }

        // Interpolated node anchoring. The default inverse converts the
        // Blender/tag-space camera point to node-local once; the live node
        // returns it to world space. Rest pose must therefore preserve the
        // authored point exactly, while animation belongs entirely to the node.
        {
            Halo3Matrix4x3 defaultNode{};
            defaultNode.scale = 2.0f;
            defaultNode.forward[1] = 1.0f;
            defaultNode.left[0] = -1.0f;
            defaultNode.up[2] = 1.0f;
            defaultNode.position[0] = 10.0f;
            defaultNode.position[1] = -3.0f;
            defaultNode.position[2] = 2.0f;

            // Exact affine inverse of defaultNode, in the same 0x34 layout.
            Halo3Matrix4x3 defaultInverse{};
            defaultInverse.scale = 0.5f;
            defaultInverse.forward[1] = -1.0f;
            defaultInverse.left[0] = 1.0f;
            defaultInverse.up[2] = 1.0f;
            defaultInverse.position[0] = 1.5f;
            defaultInverse.position[1] = 5.0f;
            defaultInverse.position[2] = -1.0f;

            const float authored[3] = {9.64f, -1.76f, 3.62f};
            const float expectedNodeLocal[3] = {0.62f, 0.18f, 0.81f};
            float recoveredLocal[3] = {};
            float restAnchor[3] = {};
            const bool restOk = Halo3MatrixInverseTransformPoint(defaultNode, authored, recoveredLocal) &&
                                Halo3ComputeNodeAnchoredPoint(defaultInverse, defaultNode, authored, restAnchor);
            const bool restExact = std::fabs(recoveredLocal[0] - expectedNodeLocal[0]) < 1e-5f &&
                                   std::fabs(recoveredLocal[1] - expectedNodeLocal[1]) < 1e-5f &&
                                   std::fabs(recoveredLocal[2] - expectedNodeLocal[2]) < 1e-5f &&
                                   std::fabs(restAnchor[0] - authored[0]) < 1e-5f &&
                                   std::fabs(restAnchor[1] - authored[1]) < 1e-5f &&
                                   std::fabs(restAnchor[2] - authored[2]) < 1e-5f;

            // The live node translates and rotates 180 degrees relative to the
            // rest node; no object-root interpolation participates.
            Halo3Matrix4x3 liveNode{};
            liveNode.scale = 2.0f;
            liveNode.forward[1] = -1.0f;
            liveNode.left[0] = 1.0f;
            liveNode.up[2] = 1.0f;
            liveNode.position[0] = -4.0f;
            liveNode.position[1] = 12.0f;
            liveNode.position[2] = 1.0f;
            float animatedAnchor[3] = {};
            const bool animatedOk = Halo3ComputeNodeAnchoredPoint(defaultInverse, liveNode, authored, animatedAnchor);
            const bool animationFollowed = std::fabs(animatedAnchor[0] - (-3.64f)) < 1e-5f &&
                                           std::fabs(animatedAnchor[1] - 10.76f) < 1e-5f &&
                                           std::fabs(animatedAnchor[2] - 2.62f) < 1e-5f;

            // Head translation is latched in this same node space. On latch it
            // contributes zero; subsequent head-local delta passes through
            // exactly while the authored baseline remains unchanged.
            const float headReference[3] = {0.10f, -0.05f, 0.20f};
            float latchHeadWorld[3] = {};
            const bool latchHeadOk = Halo3MatrixTransformPoint(liveNode, headReference, latchHeadWorld);
            float latchCamera[3] = {};
            const bool latchCameraOk = Halo3ComputeHeadParentedPoint(
                liveNode, latchHeadWorld, headReference, expectedNodeLocal, 1.0f, latchCamera);
            const bool latchExact = std::fabs(latchCamera[0] - animatedAnchor[0]) < 1e-5f &&
                                    std::fabs(latchCamera[1] - animatedAnchor[1]) < 1e-5f &&
                                    std::fabs(latchCamera[2] - animatedAnchor[2]) < 1e-5f;

            const float movedHeadLocal[3] = {0.16f, -0.07f, 0.24f};
            float movedHeadWorld[3] = {};
            const bool movedHeadOk = Halo3MatrixTransformPoint(liveNode, movedHeadLocal, movedHeadWorld);
            float movedCamera[3] = {};
            const bool movedCameraOk = Halo3ComputeHeadParentedPoint(
                liveNode, movedHeadWorld, headReference, expectedNodeLocal, 1.0f, movedCamera);
            const bool headMotionFollowed = std::fabs(movedCamera[0] - (-3.68f)) < 1e-5f &&
                                            std::fabs(movedCamera[1] - 10.64f) < 1e-5f &&
                                            std::fabs(movedCamera[2] - 2.70f) < 1e-5f;

            // Changing the live rendered node after H0 is frozen must not
            // reintroduce an object/root parent or preserve any old world-space
            // offset. Both the authored point and head delta move exactly once
            // through the new node.
            Halo3Matrix4x3 laterNode{};
            laterNode.scale = 1.5f;
            laterNode.forward[1] = 1.0f;
            laterNode.left[0] = -1.0f;
            laterNode.up[2] = 1.0f;
            laterNode.position[0] = 5.0f;
            laterNode.position[1] = -2.0f;
            laterNode.position[2] = 7.0f;
            float laterHeadWorld[3] = {};
            float laterCamera[3] = {};
            const bool laterNodeOk = Halo3MatrixTransformPoint(laterNode, movedHeadLocal, laterHeadWorld) &&
                                     Halo3ComputeHeadParentedPoint(laterNode, laterHeadWorld, headReference,
                                         expectedNodeLocal, 1.0f, laterCamera) &&
                                     std::fabs(laterCamera[0] - 4.76f) < 1e-5f &&
                                     std::fabs(laterCamera[1] - (-0.98f)) < 1e-5f &&
                                     std::fabs(laterCamera[2] - 8.275f) < 1e-5f;

            // A frame-count debounce froze H0 during the boarding animation.
            // The settle latch instead needs both concrete-seat age and a
            // continuous node-local quiet window. A reset (the action taken on
            // a concrete seat switch) must start both clocks again.
            Halo3HeadSettleLatch settle;
            const float enteringHead[3] = {0.10f, -0.05f, 0.70f};
            const float seatedHead[3] = {0.10f, -0.05f, 0.20f};
            const bool settleHeldInitial = !settle.Update(1000, enteringHead) && !settle.Update(2200, seatedHead) &&
                                           !settle.Update(2699, seatedHead);
            const bool settleLatched = settle.Update(2700, seatedHead) && settle.valid &&
                                       std::fabs(settle.reference[0] - seatedHead[0]) < 1e-6f &&
                                       std::fabs(settle.reference[1] - seatedHead[1]) < 1e-6f &&
                                       std::fabs(settle.reference[2] - seatedHead[2]) < 1e-6f;
            const float safeBounce[3] = {seatedHead[0] + 0.079f, seatedHead[1], seatedHead[2]};
            const float animationJump[3] = {seatedHead[0] + 0.081f, seatedHead[1], seatedHead[2]};
            const bool headDeltaBounded = Halo3HeadLocalDeltaWithinLimit(safeBounce, settle.reference) &&
                                          !Halo3HeadLocalDeltaWithinLimit(animationJump, settle.reference);

            // The same boundary, but the contribution SATURATES there
            // rather than dropping to zero. Inside the limit the clamp must be
            // the identity, so it agrees exactly with the old gate; outside it
            // must land exactly ON the limit, which makes the function
            // continuous and the camera unable to step.
            float insideDelta[3] = {}, outsideDelta[3] = {}, edgeDelta[3] = {};
            const float farAnimation[3] = {seatedHead[0] + 3.0f, seatedHead[1] - 4.0f, seatedHead[2]};
            const float atEdge[3] = {seatedHead[0] + kHalo3HeadMaximumLocalDelta, seatedHead[1], seatedHead[2]};
            const bool clampIdentityInside =
                Halo3ClampedHeadLocalDelta(safeBounce, settle.reference, 1.0f, insideDelta) &&
                std::fabs(insideDelta[0] - 0.079f) < 1e-6f && std::fabs(insideDelta[1]) < 1e-6f &&
                std::fabs(insideDelta[2]) < 1e-6f;
            // 3/-4/0 has length 5; clamped it must be the same direction with
            // length exactly kHalo3HeadMaximumLocalDelta.
            const bool clampSaturates =
                Halo3ClampedHeadLocalDelta(farAnimation, settle.reference, 1.0f, outsideDelta) &&
                std::fabs(outsideDelta[0] - kHalo3HeadMaximumLocalDelta * 0.6f) < 1e-6f &&
                std::fabs(outsideDelta[1] + kHalo3HeadMaximumLocalDelta * 0.8f) < 1e-6f &&
                std::fabs(outsideDelta[2]) < 1e-6f;
            // Continuity at the boundary is the whole point: approaching the
            // limit from inside and sitting exactly on it must agree, so no
            // amount of head movement can produce a step.
            const float justInside[3] = {
                seatedHead[0] + kHalo3HeadMaximumLocalDelta - 1e-5f, seatedHead[1], seatedHead[2]};
            float justInsideDelta[3] = {};
            const bool clampContinuous =
                Halo3ClampedHeadLocalDelta(atEdge, settle.reference, 1.0f, edgeDelta) &&
                Halo3ClampedHeadLocalDelta(justInside, settle.reference, 1.0f, justInsideDelta) &&
                std::fabs(edgeDelta[0] - kHalo3HeadMaximumLocalDelta) < 1e-6f &&
                std::fabs(edgeDelta[0] - justInsideDelta[0]) < 1e-4f;
            // A non-finite sample must refuse rather than emit a partial or
            // stale delta, so the caller keeps the exact authored point.
            const float brokenHead[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
            float brokenDelta[3] = {};
            const bool clampRefusesBroken =
                !Halo3ClampedHeadLocalDelta(brokenHead, settle.reference, 1.0f, brokenDelta) &&
                !Halo3ClampedHeadLocalDelta(safeBounce, settle.reference, 1.0f, nullptr);
            // Gain is a plain strength scale on the clamped result. Half
            // gain must be exactly half the travel, zero must bolt the view to
            // the seat, and an out-of-range or non-finite gain must not be able
            // to amplify the bounce past its full setting.
            float halfDelta[3] = {}, zeroDelta[3] = {}, overDelta[3] = {};
            float nanGainDelta[3] = {};
            const bool gainScales = Halo3ClampedHeadLocalDelta(safeBounce, settle.reference, 0.5f, halfDelta) &&
                                    std::fabs(halfDelta[0] - 0.0395f) < 1e-6f &&
                                    Halo3ClampedHeadLocalDelta(safeBounce, settle.reference, 0.0f, zeroDelta) &&
                                    std::fabs(zeroDelta[0]) < 1e-9f && std::fabs(zeroDelta[1]) < 1e-9f &&
                                    std::fabs(zeroDelta[2]) < 1e-9f;
            const bool gainBounded = Halo3ClampedHeadLocalDelta(farAnimation, settle.reference, 5.0f, overDelta) &&
                                     std::fabs(overDelta[0] - kHalo3HeadMaximumLocalDelta * 0.6f) < 1e-6f &&
                                     !Halo3ClampedHeadLocalDelta(safeBounce, settle.reference,
                                         std::numeric_limits<float>::quiet_NaN(), nanGainDelta);
            Check(clampIdentityInside && clampSaturates && clampContinuous && clampRefusesBroken && gainScales &&
                      gainBounded,
                "The occupant-head contribution is the identity inside its "
                "limit, saturates exactly on it, is continuous across it, "
                "scales linearly with the bounce strength without exceeding "
                "full travel, and refuses a non-finite sample or gain");
            settle = {};
            const bool seatSwitchRestarts =
                !settle.Update(2701, seatedHead) && !settle.valid && settle.observedSinceMs == 2701;

            Halo3SeatPositionKey positionKey;
            const bool positionKeyStartsEmpty =
                !positionKey.Matches(7, 0x12340002, 0, static_cast<uint32_t>(Halo3VehicleId::Warthog), false);
            positionKey.Set(7, 0x12340002, 0, static_cast<uint32_t>(Halo3VehicleId::Warthog), false);
            const bool positionKeyConcrete =
                positionKey.Matches(7, 0x12340002, 0, static_cast<uint32_t>(Halo3VehicleId::Warthog), false) &&
                !positionKey.Matches(8, 0x12340002, 0, static_cast<uint32_t>(Halo3VehicleId::Warthog), false) &&
                !positionKey.Matches(7, 0x12340003, 0, static_cast<uint32_t>(Halo3VehicleId::Warthog), false) &&
                !positionKey.Matches(7, 0x12340002, 1, static_cast<uint32_t>(Halo3VehicleId::Warthog), false) &&
                !positionKey.Matches(7, 0x12340002, 0, static_cast<uint32_t>(Halo3VehicleId::Mongoose), false) &&
                !positionKey.Matches(7, 0x12340002, 0, static_cast<uint32_t>(Halo3VehicleId::Warthog), true);

            // Actual Warthog hull node fixture from the official exported
            // render-model tag. This pins the scale-first +0x28 interpretation
            // and proves a configured vehicle-local placement trim survives
            // D^-1 -> D without changing axes or being applied twice.
            Halo3Matrix4x3 hogDefault{};
            hogDefault.scale = 1.0f;
            hogDefault.forward[0] = 1.0f;
            hogDefault.left[1] = 1.0f;
            hogDefault.up[2] = 1.0f;
            hogDefault.position[0] = -0.0250015f;
            hogDefault.position[1] = -7.03e-10f;
            hogDefault.position[2] = 0.42f;
            Halo3Matrix4x3 hogInverse{};
            hogInverse.scale = 1.0f;
            hogInverse.forward[0] = 1.0f;
            hogInverse.left[1] = 1.0f;
            hogInverse.up[2] = 1.0f;
            hogInverse.position[0] = 0.0250015f;
            hogInverse.position[1] = 7.03e-10f;
            hogInverse.position[2] = -0.42f;
            const float hogAdjustedPoint[3] = {0.0299f + 0.0328084f, 0.1683f - 0.0492126f, 0.6663f + 0.0164042f};
            float hogRestAnchor[3] = {};
            const bool actualTagFixture =
                Halo3ComputeNodeAnchoredPoint(hogInverse, hogDefault, hogAdjustedPoint, hogRestAnchor) &&
                std::fabs(hogRestAnchor[0] - hogAdjustedPoint[0]) < 1e-6f &&
                std::fabs(hogRestAnchor[1] - hogAdjustedPoint[1]) < 1e-6f &&
                std::fabs(hogRestAnchor[2] - hogAdjustedPoint[2]) < 1e-6f;

            // Every public helper refuses torn/non-finite matrices and points.
            Halo3Matrix4x3 nonfinite = liveNode;
            nonfinite.position[1] = std::numeric_limits<float>::quiet_NaN();
            Halo3Matrix4x3 zeroScale = liveNode;
            zeroScale.scale = 0.0f;
            Halo3Matrix4x3 badBasis = liveNode;
            badBasis.left[0] = 0.0f;
            badBasis.left[1] = -1.0f; // duplicates forward
            const float nonfinitePoint[3] = {0.0f, std::numeric_limits<float>::infinity(), 0.0f};
            float rejected[3] = {91.0f, 92.0f, 93.0f};
            const bool invalidRejected =
                Halo3MatrixCountFromByteSize(0x34) == 1 && Halo3MatrixCountFromByteSize(0x68) == 2 &&
                Halo3MatrixCountFromByteSize(0x35) == 0 && Halo3MatrixCountFromByteSize(0) == 0 &&
                !Halo3MatrixValid(nonfinite) && !Halo3MatrixValid(zeroScale) && !Halo3MatrixValid(badBasis) &&
                !Halo3MatrixTransformPoint(nonfinite, authored, rejected) &&
                !Halo3MatrixInverseTransformPoint(zeroScale, authored, rejected) &&
                !Halo3ComputeNodeAnchoredPoint(defaultInverse, badBasis, authored, rejected) &&
                !Halo3ComputeHeadParentedPoint(
                    liveNode, nonfinitePoint, headReference, expectedNodeLocal, 1.0f, rejected) &&
                rejected[0] == 91.0f && rejected[1] == 92.0f && rejected[2] == 93.0f;

            Check(restOk && restExact && animatedOk && animationFollowed && latchHeadOk && latchCameraOk &&
                      latchExact && movedHeadOk && movedCameraOk && headMotionFollowed && laterNodeOk &&
                      settleHeldInitial && settleLatched && headDeltaBounded && seatSwitchRestarts &&
                      positionKeyStartsEmpty && positionKeyConcrete && actualTagFixture && invalidRejected,
                "Halo 3 live node anchoring preserves the exact authored point, "
                "waits out entry motion, inherits mesh/head-local motion once, "
                "and rejects invalid data");
        }

        // Frame smoothing. MEASURED: the camera samples at 240/sec while
        // the hull only moves at 60, so three of every four published frames
        // must be walked toward the next simulation state instead of repeating
        // the last one. The output has to be continuous across a tick boundary
        // (that discontinuity IS the reported shaking), must never overshoot,
        // and must snap rather than slide on a teleport.
        {
            auto frameAt = [](float x, float yawDeg)
            {
                Halo3Frame f{};
                f.pos[0] = x;
                f.pos[1] = 0.0f;
                f.pos[2] = 0.0f;
                const float r = yawDeg / 57.2957795f;
                f.fwd[0] = std::cos(r);
                f.fwd[1] = std::sin(r);
                f.fwd[2] = 0.0f;
                f.up[0] = 0.0f;
                f.up[1] = 0.0f;
                f.up[2] = 1.0f;
                return f;
            };
            // 60 Hz simulation, 240 Hz camera: the hull advances 0.1 wu and 2
            // degrees per tick and is sampled four times per tick.
            Halo3FrameInterp interp;
            const double dt = 1.0 / 240.0;
            double now = 0.0;
            bool smooth = true;
            bool ordered = true;
            float lastX = 0.0f;
            float biggestStep = 0.0f;
            for (int tick = 0; tick < 30; ++tick)
            {
                const Halo3Frame raw = frameAt(tick * 0.1f, tick * 2.0f);
                for (int sub = 0; sub < 4; ++sub)
                {
                    const Halo3Frame out = Halo3InterpolateFrame(interp, raw, now, true);
                    now += dt;
                    if (tick < 3) // the tick length is still being learned
                    {
                        lastX = out.pos[0];
                        continue;
                    }
                    const float step = out.pos[0] - lastX;
                    if (step < -1e-4f)
                        ordered = false; // never runs backwards
                    if (step > biggestStep)
                        biggestStep = step;
                    lastX = out.pos[0];
                    if (!Halo3FrameOrthonormal(out))
                        smooth = false; // blended basis is real
                }
            }
            // A raw feed would stand still for three frames then jump the whole
            // 0.1 wu. Smoothed, no single frame may move much more than the
            // even quarter-tick share of it.
            const bool evened = biggestStep < 0.045f && ordered;
            // At rest the output settles exactly on the newest state. The authority fade
            // makes that settling gradual on purpose — when ticks stop, the
            // extrapolation authority glides away over ~40 ms rather than
            // snapping the seat back — so allow it that time.
            Halo3Frame settled{};
            for (int i = 0; i < 200; ++i)
            {
                settled = Halo3InterpolateFrame(interp, frameAt(29 * 0.1f, 29 * 2.0f), now, true);
                now += dt;
            }
            const bool settles = std::fabs(settled.pos[0] - 2.9f) < 1e-4f;
            // A teleport snaps. Sliding a camera across a level would put the
            // player's head inside the geometry between the two ends.
            const Halo3Frame teleported = frameAt(400.0f, 58.0f);
            const Halo3Frame jumped = Halo3InterpolateFrame(interp, teleported, now + dt, true);
            const bool snaps = std::fabs(jumped.pos[0] - 400.0f) < 1e-3f;
            // Switched off, the published frame is exactly the raw one.
            Halo3FrameInterp off;
            const Halo3Frame passthrough = Halo3InterpolateFrame(off, frameAt(7.0f, 10.0f), now, false);
            const bool bypasses = passthrough.pos[0] == 7.0f && !off.valid;
            // Blending two orientations component-wise is not a rotation; the
            // result must still be a usable basis.
            const Halo3Frame mid = Halo3LerpFrame(frameAt(0.0f, 0.0f), frameAt(1.0f, 40.0f), 0.5f);
            const bool basisOk = Halo3FrameOrthonormal(mid) &&
                                 std::fabs(std::atan2(mid.fwd[1], mid.fwd[0]) * 57.2957795f - 20.0f) < 1.0f;

            // The residual a headset video exposed. A tick is only NOTICED on
            // the first camera call after it happens, which is up to a whole
            // camera interval late and a different amount late every time. If
            // the blend restarts at zero on each detection, that jitter lands
            // straight on the camera: measured in the headset as the vehicle
            // still moving on 49% of frames whose world was stationary. With
            // the hull travelling at a constant speed, every published frame
            // must advance by the same amount no matter when the tick was
            // noticed.
            {
                Halo3FrameInterp jit;
                const double camDt = 1.0 / 240.0;
                const double tickLen = 1.0 / 60.0;
                double t = 0.0;
                double simTime = 0.0;
                int simTicks = 0;
                Halo3Frame held = frameAt(0.0f, 0.0f);
                float previousX = 0.0f;
                bool first = true;
                double worst = 0.0, total = 0.0;
                int counted = 0;
                // Camera and simulation on independent clocks: the camera runs
                // a hair off 240 Hz, so the detection lag walks through the
                // whole range instead of sitting still.
                for (int i = 0; i < 2000; ++i)
                {
                    t += camDt * 1.017;
                    while (simTime + tickLen <= t)
                    {
                        simTime += tickLen;
                        ++simTicks;
                        held = frameAt(simTicks * 0.1f, 0.0f);
                    }
                    const Halo3Frame out = Halo3InterpolateFrame(jit, held, t, true);
                    if (i > 600) // let the phase settle
                    {
                        if (!first)
                        {
                            const double d = out.pos[0] - previousX;
                            total += d;
                            ++counted;
                            const double ideal = 0.1 * (camDt * 1.017) / tickLen;
                            const double err = std::fabs(d - ideal);
                            if (err > worst)
                                worst = err;
                        }
                        first = false;
                    }
                    previousX = out.pos[0];
                }
                const double ideal = 0.1 * (camDt * 1.017) / tickLen;
                // Worst single-frame deviation under a quarter of the even
                // share. Restarting the blend at each detection puts it at a
                // full share or more.
                const bool locked =
                    counted > 1000 && worst < ideal * 0.25 && std::fabs(total / counted - ideal) < ideal * 0.05;
                Check(locked, "Seat frame advances evenly even though each simulation "
                              "tick is noticed a different amount late");
            }

            Check(smooth && evened && settles && snaps && bypasses && basisOk,
                "Seat frame is walked across the 60Hz simulation tick, settles "
                "on the newest state, snaps on a teleport, and stays a real "
                "orthonormal basis");
        }

        // Photon-time prediction: a 120 Hz headset needs more than the 60 Hz
        // simulation. An older bounding-centre witness logged a zero render lead
        // on real hardware, but that mechanism never measured anything, so it is
        // now diagnostic-only.
        //
        // Every frame is DISPLAYED one refresh period after it is built, and
        // OpenXR already predicts the head that far ahead. The seat must be
        // predicted by the same interval or it trails the head, worse the
        // faster the vehicle moves.
        {
            auto hullAt = [](float x)
            {
                Halo3Frame f{};
                f.pos[0] = x;
                f.fwd[0] = 1.0f;
                f.up[2] = 1.0f;
                return f;
            };
            const double tickLen = 1.0 / 60.0;
            [[maybe_unused]] const float stepWu = 0.1f; // wu per tick = wu per 16.7 ms

            // The seat is PARENTED to the car. With smoothing off — the
            // default — every published frame is EXACTLY the simulation's own
            // hull state, so the seat is rigidly bolted to the car and you
            // feel its physics unaltered.
            {
                Halo3FrameInterp rigid;
                const double camDt = 1.0 / 251.0;
                double t = 0.0, simTime = 0.0;
                int ticks = 0;
                Halo3Frame held = hullAt(0.0f);
                bool exact = true;
                for (int i = 0; i < 2000; ++i)
                {
                    while (simTime + tickLen <= t)
                    {
                        simTime += tickLen;
                        ++ticks;
                        held = hullAt(static_cast<float>(ticks * 0.30));
                    }
                    const Halo3Frame out = Halo3InterpolateFrame(rigid, held, t, false);
                    if (out.pos[0] != held.pos[0])
                        exact = false;
                    t += camDt;
                }
                Check(exact && !rigid.valid, "With smoothing off the seat is bolted to the car: every "
                                             "published frame is exactly the simulation's own hull "
                                             "state, physics and all");
            }

            // With smoothing on, the blend must stay strictly BETWEEN the two
            // most recent simulation states. Everything that used to live past
            // the newest state was removed: a bounded blend that is held at a
            // ceiling while the hull keeps turning loses that rotation for
            // good, which is what let the car rotate out from under the
            // camera ("i was able to drift the camera completely around").
            {
                Halo3FrameInterp interp;
                const double camDt = 1.0 / 251.0;
                const double boostStep = 0.30;
                double t = 0.0, simTime = 0.0;
                int ticks = 0;
                Halo3Frame held = hullAt(0.0f);
                bool bounded = true, ordered = true;
                float last = 0.0f;
                bool have = false;
                for (int i = 0; i < 4000; ++i)
                {
                    while (simTime + tickLen <= t)
                    {
                        simTime += tickLen;
                        ++ticks;
                        held = hullAt(static_cast<float>(ticks * boostStep));
                    }
                    const Halo3Frame out = Halo3InterpolateFrame(interp, held, t, true);
                    if (i > 600)
                    {
                        // Never past the newest state, never behind the one
                        // before it.
                        const double newest = ticks * boostStep;
                        if (out.pos[0] > newest + 1e-4 || out.pos[0] < newest - boostStep - 1e-4)
                            bounded = false;
                        if (have && out.pos[0] < last - 1e-4f)
                            ordered = false;
                    }
                    last = out.pos[0];
                    have = true;
                    t += camDt;
                }
                Check(bounded && ordered && interp.lastAlpha <= 1.0f,
                    "Smoothed, the seat stays strictly between the two most "
                    "recent simulation states and never runs backwards - "
                    "nothing extrapolates past the car any more");
            }

            // The view-follow integrates the heading step it is handed, so it
            // must be fed the RAW hull heading. Driving a full circle must
            // fold in exactly one full turn: any rotation the source fails to
            // deliver is lost for good and reads as the car slowly rotating
            // out from under the camera.
            {
                Halo3SeatYaw yaw;
                float fwd[3] = {1.0f, 0.0f, 0.0f};
                double total = 0.0;
                const int steps = 720;
                for (int i = 0; i <= steps; ++i)
                {
                    const double a = 6.283185307 * i / steps;
                    fwd[0] = static_cast<float>(std::cos(a));
                    fwd[1] = static_cast<float>(std::sin(a));
                    total += Halo3SeatYawDelta(yaw, fwd, true);
                }
                Check(std::fabs(total - 6.283185307) < 1e-3, "A full circle folds exactly one full turn into the view "
                                                             "reference, so the car can never rotate out from under "
                                                             "the camera");
            }
            // The pacing guarantee: an evenly advancing hull must produce evenly
            // advancing frames even though each tick is noticed a different
            // amount late.
            {
                Halo3FrameInterp jit;
                const double camDt = 1.0 / 240.0;
                double t = 0.0, simTime = 0.0;
                int simTicks = 0;
                Halo3Frame held = hullAt(0.0f);
                float previousX = 0.0f;
                bool first = true;
                double worst = 0.0, total = 0.0;
                int counted = 0;
                for (int i = 0; i < 2000; ++i)
                {
                    t += camDt * 1.017;
                    while (simTime + tickLen <= t)
                    {
                        simTime += tickLen;
                        ++simTicks;
                        held = hullAt(simTicks * 0.1f);
                    }
                    const Halo3Frame out = Halo3InterpolateFrame(jit, held, t, true);
                    if (i > 600)
                    {
                        if (!first)
                        {
                            const double d = out.pos[0] - previousX;
                            total += d;
                            ++counted;
                            const double ideal = 0.1 * (camDt * 1.017) / tickLen;
                            const double err = std::fabs(d - ideal);
                            if (err > worst)
                                worst = err;
                        }
                        first = false;
                    }
                    previousX = out.pos[0];
                }
                const double ideal = 0.1 * (camDt * 1.017) / tickLen;
                Check(counted > 1000 && worst < ideal * 0.25 && std::fabs(total / counted - ideal) < ideal * 0.05,
                    "Seat frame advances evenly even though each simulation "
                    "tick is noticed a different amount late");
            }
        }

        // Seat yaw: the view must turn with the hull. The function returns
        // the yaw STEP to fold into the shared reference, so what matters is
        // that the accumulated steps equal the hull's rotation, that entry and
        // exit re-reference instead of snapping, and that no single frame can
        // whip the view.
        {
            auto hullFwd = [](float yaw, float out[3])
            {
                out[0] = std::cos(yaw);
                out[1] = std::sin(yaw);
                out[2] = 0.0f;
            };
            float fwd[3];
            Halo3SeatYaw seat;
            hullFwd(1.0f, fwd);
            const bool idleZero = Halo3SeatYawDelta(seat, fwd, false) == 0.0f && !seat.armed;
            // Sitting down at heading 1.0 rad: this heading is straight ahead.
            const bool entryZero = Halo3SeatYawDelta(seat, fwd, true) == 0.0f && seat.armed;
            // Hull swings 20 deg left -> the view follows by exactly that.
            hullFwd(1.0f + 0.34906585f, fwd);
            const bool followsHull = std::fabs(Halo3SeatYawDelta(seat, fwd, true) - 0.34906585f) < 1e-4f;
            // Getting out re-references: sitting in a car facing the other way
            // must read straight ahead again, not 180 degrees out.
            Halo3SeatYawDelta(seat, fwd, false);
            hullFwd(-2.5f, fwd);
            const bool reentryZero = !seat.armed && Halo3SeatYawDelta(seat, fwd, true) == 0.0f;

            // Two full left turns at full weight, three degrees at a time: the
            // steps sum to the hull's whole rotation and none of them is a
            // jump. This is the wrap test — a wrapped TOTAL scaled by weight
            // would snap by 2*pi*weight every time the hull passed +/-pi.
            Halo3SeatYaw spin;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(spin, fwd, true);
            bool fullOk = true;
            double sum = 0.0;
            for (int i = 1; i <= 240; ++i)
            {
                hullFwd(Halo3WrapPi(static_cast<float>(i) * 0.05236f), fwd);
                const float step = Halo3SeatYawDelta(spin, fwd, true);
                if (std::fabs(step) > 0.1f)
                    fullOk = false;
                sum += step;
            }
            fullOk = fullOk && std::fabs(sum - 240.0 * 0.05236) < 1e-2;
            // Toggle-off is represented by the caller making the transaction
            // inactive. It clears the old reference, so turning it back on in
            // the current seat establishes a fresh zero without a snap.
            Halo3SeatYaw off;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(off, fwd, true);
            hullFwd(0.3f, fwd);
            const bool toggleOff = Halo3SeatYawDelta(off, fwd, false) == 0.0f && !off.armed &&
                                   Halo3SeatYawDelta(off, fwd, true) == 0.0f && off.armed;

            // This is a reported collision/spin regression. A legitimate
            // same-car 2.6-rad step must be consumed in full; the old 0.60-rad
            // cap discarded it and left the car permanently rotated under the
            // view. A concrete seat switch is reset explicitly instead.
            Halo3SeatYaw collision;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(collision, fwd, true);
            hullFwd(2.6f, fwd);
            const bool collisionFollowed = std::fabs(Halo3SeatYawDelta(collision, fwd, true) - 2.6f) < 1e-4f;
            hullFwd(2.7f, fwd);
            const bool continuesAfterCollision = std::fabs(Halo3SeatYawDelta(collision, fwd, true) - 0.1f) < 1e-4f;
            Halo3SeatYaw switched;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(switched, fwd, true);
            Halo3SeatYawDelta(switched, fwd, false);
            hullFwd(2.6f, fwd);
            const bool seatSwitchRearmed = Halo3SeatYawDelta(switched, fwd, true) == 0.0f;

            // A Banshee pulled through the vertical has no heading at all;
            // hold the last valid yaw, then catch up when heading exists again.
            Halo3SeatYaw vertical;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(vertical, fwd, true);
            float nose[3] = {0.02f, 0.0f, 0.9998f};
            const bool verticalHeld = Halo3SeatYawDelta(vertical, nose, true) == 0.0f && vertical.armed;
            hullFwd(0.9f, fwd);
            const bool verticalCaughtUp = std::fabs(Halo3SeatYawDelta(vertical, fwd, true) - 0.9f) < 1e-4f;

            Halo3SeatYaw junk;
            hullFwd(0.0f, fwd);
            Halo3SeatYawDelta(junk, fwd, true);
            float bad[3] = {std::nanf(""), 0.0f, 0.0f};
            const bool junkSafe = Halo3SeatYawDelta(junk, bad, true) == 0.0f && junk.armed;
            hullFwd(0.5f, fwd);
            const bool junkCaughtUp = std::fabs(Halo3SeatYawDelta(junk, fwd, true) - 0.5f) < 1e-4f;

            Check(idleZero && entryZero && followsHull && reentryZero && fullOk && toggleOff && collisionFollowed &&
                      continuesAfterCollision && seatSwitchRearmed && verticalHeld && verticalCaughtUp && junkSafe &&
                      junkCaughtUp,
                "Seat yaw follows every same-car collision/spin step, resets "
                "only at an explicit ownership boundary, and catches up after "
                "invalid or degenerate samples");
        }

        // Tracked poses compose inside roll-free vehicle yaw+pitch.
        {
            const auto approx = [](float a, float b) { return std::fabs(a - b) <= 1.0e-4f; };
            const float hill = 20.0f / 57.2957795f;
            float nose[3] = {std::cos(hill), 0.0f, std::sin(hill)};
            float pitch = 0.0f;
            float badNose[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
            const bool pitchSafe = Halo3RollStablePitchFromForward(nose, pitch) && approx(pitch, hill) &&
                                   !Halo3RollStablePitchFromForward(badNose, pitch);
            float hillBasis[9], sideBasis[9];
            const bool hillSafe = Halo3ComposeRollStableFollowBasis(0.0f, hill, 0.0f, 0.0f, 0.0f, 0.0f, hillBasis) &&
                                  approx(hillBasis[0], std::cos(hill)) && approx(hillBasis[2], std::sin(hill)) &&
                                  approx(hillBasis[6], -std::sin(hill)) && approx(hillBasis[8], std::cos(hill));
            // A 90-degree local look points across the hill, not uphill.
            const bool sideSafe =
                Halo3ComposeRollStableFollowBasis(0.0f, hill, 0.0f, 1.5707963268f, 0.0f, 0.0f, sideBasis) &&
                approx(sideBasis[0], 0.0f) && approx(sideBasis[1], 1.0f) && approx(sideBasis[2], 0.0f);
            float composedAim[9], recoveredYaw = 0.0f, recoveredPitch = 0.0f;
            constexpr float aimHullYaw = 0.7f;
            constexpr float aimHullPitch = 0.25f;
            constexpr float aimGameYawRef = 1.1f;
            constexpr float aimTrackedYaw = -0.4f;
            constexpr float aimTrackedPitch = 0.2f;
            const bool inverseFollowSafe = Halo3ComposeRollStableFollowBasis(aimHullYaw, aimHullPitch, aimGameYawRef,
                                               aimTrackedYaw, aimTrackedPitch, 0.0f, composedAim) &&
                                           Halo3InverseRollStableFollowForward(composedAim, aimHullYaw, aimHullPitch,
                                               aimGameYawRef, recoveredYaw, recoveredPitch) &&
                                           approx(recoveredYaw, aimTrackedYaw) &&
                                           approx(recoveredPitch, aimTrackedPitch);
            float flatAim[9], flatYaw = 0.0f, flatPitch = 0.0f;
            constexpr float flatHullYaw = -0.3f;
            constexpr float flatGameYawRef = 0.4f;
            constexpr float flatTrackedYaw = 0.55f;
            constexpr float flatTrackedPitch = -0.25f;
            const bool inverseFlatSafe =
                Halo3ComposeRollStableFollowBasis(
                    flatHullYaw, 0.0f, flatGameYawRef, flatTrackedYaw, flatTrackedPitch, 0.0f, flatAim) &&
                Halo3InverseRollStableFollowForward(flatAim, flatHullYaw, 0.0f, flatGameYawRef, flatYaw, flatPitch) &&
                approx(flatYaw, flatTrackedYaw) && approx(flatPitch, flatTrackedPitch);
            const float zeroAim[3] = {};
            const bool inverseRejectsZero =
                !Halo3InverseRollStableFollowForward(zeroAim, 0.0f, 0.0f, 0.0f, recoveredYaw, recoveredPitch);
            const bool inverseRejectsNonfinite =
                !Halo3InverseRollStableFollowForward(badNose, 0.0f, 0.0f, 0.0f, recoveredYaw, recoveredPitch);
            const bool pitchSeatPolicy = Halo3SeatFollowsPitch(Halo3VehicleId::Warthog, 0, false) &&
                                         Halo3SeatFollowsPitch(Halo3VehicleId::Warthog, 1, false) &&
                                         Halo3SeatFollowsPitch(Halo3VehicleId::Scorpion, 0, false) &&
                                         !Halo3SeatFollowsPitch(Halo3VehicleId::Hornet, 1, false) &&
                                         !Halo3SeatFollowsPitch(Halo3VehicleId::Banshee, 0, false) &&
                                         !Halo3SeatFollowsPitch(Halo3VehicleId::Hornet, 0, false) &&
                                         !Halo3SeatFollowsPitch(Halo3VehicleId::Unknown, 0, false);
            Check(pitchSafe && hillSafe && sideSafe && pitchSeatPolicy,
                "Vehicle pitch follow is roll-stable and seat-safe");
            const bool inverseSafe =
                inverseFollowSafe && inverseFlatSafe && inverseRejectsZero && inverseRejectsNonfinite;
            Check(inverseSafe, "World-space vehicle aim inverts to tracked yaw/pitch");
        }

        // Steering ownership. The seat that authors steering must be
        // exactly the seat where the hull-follow reference destroys the closed
        // loop's feedback: a look-steered vehicle's driver, and only while the
        // view actually follows.
        {
            const bool drivers = Halo3SeatIsDriver(Halo3VehicleId::Warthog, 0, false) &&
                                 !Halo3SeatIsDriver(Halo3VehicleId::Warthog, 1, false) &&
                                 !Halo3SeatIsDriver(Halo3VehicleId::Warthog, 0, true) &&
                                 !Halo3SeatIsDriver(Halo3VehicleId::StationaryTurret, 0, false) &&
                                 !Halo3SeatIsDriver(Halo3VehicleId::Unknown, 0, false);
            // The Scorpion and Wraith steer from the left stick and aim a
            // turret with the right: their closed loop keeps real feedback and
            // must never be taken over.
            const bool families = Halo3VehicleIsLookSteered(Halo3VehicleId::Warthog) &&
                                  Halo3VehicleIsLookSteered(Halo3VehicleId::Chopper) &&
                                  Halo3VehicleIsLookSteered(Halo3VehicleId::Banshee) &&
                                  !Halo3VehicleIsLookSteered(Halo3VehicleId::Scorpion) &&
                                  !Halo3VehicleIsLookSteered(Halo3VehicleId::Wraith) &&
                                  !Halo3VehicleIsLookSteered(Halo3VehicleId::StationaryTurret) &&
                                  !Halo3VehicleIsLookSteered(Halo3VehicleId::Unknown);
            // Aircraft are look-steered but take the plain flight stick.
            const bool wheelSeats =
                Halo3VehicleUsesWheel(Halo3VehicleId::Warthog) && Halo3VehicleUsesWheel(Halo3VehicleId::Mongoose) &&
                Halo3VehicleUsesWheel(Halo3VehicleId::Ghost) && Halo3VehicleUsesWheel(Halo3VehicleId::Mauler) &&
                !Halo3VehicleUsesWheel(Halo3VehicleId::Banshee) && !Halo3VehicleUsesWheel(Halo3VehicleId::Hornet) &&
                !Halo3VehicleUsesWheel(Halo3VehicleId::Scorpion);
            const bool gate = Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 0, false, true) &&
                              Halo3SeatAuthorsSteering(Halo3VehicleId::Banshee, 0, false, true) &&
                              // Follow off -> the closed loop still has its feedback, so
                              // every control stays exactly as it shipped.
                              !Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 0, false, false) &&
                              !Halo3SeatAuthorsSteering(Halo3VehicleId::Scorpion, 0, false, true) &&
                              !Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 1, false, true) &&
                              !Halo3SeatAuthorsSteering(Halo3VehicleId::Warthog, 0, true, true);
            // The follow itself must never reach a seat whose aim channel
            // turns the very frame being followed while nothing replaces that
            // loop. A walk-up turret is published in its OWN object frame, so
            // it is excluded; a mounted gunner is published in the carrier's,
            // so it is fine.
            const bool follows = Halo3SeatFollowsHull(Halo3VehicleId::Warthog, 0, false) &&
                                 Halo3SeatFollowsHull(Halo3VehicleId::Warthog, 1, false) &&
                                 Halo3SeatFollowsHull(Halo3VehicleId::Warthog, 0, true) &&
                                 Halo3SeatFollowsHull(Halo3VehicleId::Scorpion, 0, false) &&
                                 !Halo3SeatFollowsHull(Halo3VehicleId::StationaryTurret, 0, false) &&
                                 !Halo3SeatFollowsHull(Halo3VehicleId::Unknown, 0, false) &&
                                 // A seat with no authored point keeps the stock chase camera,
                                 // so there is nothing there for the follow to fix.
                                 !Halo3SeatFollowsHull(Halo3VehicleId::Mongoose, 4, false);
            // Anything that authors steering must also be following, or the
            // two would disagree about which seat changed behavior.
            const bool consistent = Halo3SeatFollowsHull(Halo3VehicleId::Warthog, 0, false) &&
                                    Halo3SeatFollowsHull(Halo3VehicleId::Banshee, 0, false) &&
                                    Halo3SeatFollowsHull(Halo3VehicleId::Chopper, 0, false);
            const bool turnOwnership = Halo3VrTurnOwnsStick(false, false) && Halo3VrTurnOwnsStick(false, true) &&
                                       !Halo3VrTurnOwnsStick(true, false) && Halo3VrTurnOwnsStick(true, true);
            {
                Halo2SnapTurnState turn;
                Check(turn.Update(1, 1, 0, true, true, true, 0.9f, 30) && turn.pending &&
                          std::fabs(turn.targetYaw + 0.5235988f) < 1e-5f,
                    "H2 right snap chooses exactly one configured clockwise target");
                Check(!turn.Update(1, 1, 0, true, true, true, -0.9f, 30) &&
                          !turn.Update(1, 2, 0, true, true, true, 0.9f, 30),
                    "H2 duplicate observer/eye calls and a held stick cannot repeat a snap");
                turn.Update(1, 3, 0, true, true, true, 0, 30);
                Check(turn.Update(1, 4, 0, true, true, true, -0.9f, 30) && std::fabs(turn.targetYaw) < 1e-5f,
                    "H2 opposite deliberate snap reverses the pending target");
                turn.Update(2, 5, 3.10f, false, true, true, 0.9f, 45);
                Check(!turn.Update(2, 6, 3.10f, true, true, true, 0.9f, 45),
                    "vehicle/menu stick takeover cannot manufacture a snap");
                turn.Update(2, 7, 3.10f, true, true, true, 0, 45);
                Check(turn.Update(2, 8, 3.10f, true, true, true, -0.9f, 45) && turn.targetYaw < 0,
                    "H2 snap target wraps through pi without a full revolution");
                turn.Update(3, 9, 0, true, false, true, 0, 30);
                Check(!turn.Update(3, 10, 0, true, false, true, 0.9f, 30) && !turn.pending,
                    "H2 smooth mode keeps its native turning path");
                turn.Update(3, 11, 0, true, true, false, 0, 30);
                Check(!turn.Update(3, 12, 0, true, true, true, 0.9f, 30),
                    "tracking return while deflected waits for a centered stick");
            }
            for (float engineYaw : {0.f, -0.2f, -0.5235988f})
            {
                Halo2CameraBasis stock{};
                stock.forward[0] = std::cos(engineYaw);
                stock.forward[1] = std::sin(engineYaw);
                stock.up[2] = 1;
                Halo2TrackedHeadInput head{};
                head.worldScale = 1;
                const float identity[4]{0, 0, 0, 1}, zero[3]{};
                const float offset = -0.5235988f - engineYaw;
                Check(Halo2SnapReference(identity, offset, head.referenceOrientation),
                    "H2 snap reference remains a normalized tracking transform");
                Halo2CameraBasis view{}, gun{};
                Check(Halo2BuildTrackedCenterCamera(stock, head, view) &&
                          Halo2BuildStableControllerCarrier(
                              stock, head.referenceOrientation, zero, identity, zero, 1, 0, gun) &&
                          std::fabs(std::atan2(view.forward[1], view.forward[0]) + 0.5235988f) < 1e-5f &&
                          std::fabs(view.forward[0] - gun.forward[0]) < 1e-5f &&
                          std::fabs(view.forward[1] - gun.forward[1]) < 1e-5f,
                    "H2 view and gun stay at the snapped heading while native body yaw converges");
            }
            for (int hz : {60, 90, 120, 144})
                for (float mapping : {-1.f, 1.f})
                {
                    Halo4PitchServo servo;
                    float engine = 3.10f;
                    const float target = Halo2SnapTurnState::Wrap(engine + 0.5235988f);
                    bool parked = false;
                    for (int frame = 0; frame < hz * 4; ++frame)
                    {
                        const float continuous =
                            servo.haveLastEnginePitch
                                ? servo.lastEnginePitch + Halo2SnapTurnState::Wrap(engine - servo.lastEnginePitch)
                                : engine;
                        const float error = Halo2SnapTurnState::Wrap(target - engine);
                        const float command =
                            Halo4PitchServoStep(servo, continuous, continuous + error, kHalo4PitchServoGain);
                        if (command == 0 && servo.axis.resting)
                        {
                            parked = std::fabs(error) < 0.04f;
                            break;
                        }
                        const float raw = std::copysign(std::max(std::fabs(command), 0.275f), command);
                        engine = Halo2SnapTurnState::Wrap(engine + mapping * raw * 3.14159265f / hz);
                    }
                    Check(parked, "H2 native snap convergence parks across rates, inverted axes and yaw seam");
                }
            bool snapLatched = false;
            const bool heldTakeoverSafe = !Halo3ConsumeSnapTurn(false, 0.9f, snapLatched) && snapLatched &&
                                          !Halo3ConsumeSnapTurn(true, 0.9f, snapLatched);
            const bool deliberateSnap = !Halo3ConsumeSnapTurn(false, 0.0f, snapLatched) && !snapLatched &&
                                        Halo3ConsumeSnapTurn(true, 0.9f, snapLatched) &&
                                        !Halo3ConsumeSnapTurn(true, 0.9f, snapLatched);
            Check(drivers && families && wheelSeats && gate && follows && consistent && turnOwnership &&
                      heldTakeoverSafe && deliberateSnap,
                "Only a look-steered vehicle's driver authors steering, only "
                "while the view follows the hull; the wheel frees the turn "
                "stick, and unsafe pitch-follow seats remain excluded");
        }

        // First-person seat flag. The exact stock flag words come from the
        // official H3EK exports.
        {
            constexpr uint32_t kWarthogDriver = 0x40014u;   // bits {2,4,18}
            constexpr uint32_t kWarthogPassenger = 0x1070u; // bits {4,5,6,12}
            constexpr uint32_t kMachinegunTurret = 0x5500018u;
            // Every stock player seat is third person, which is why Halo has no
            // first-person vehicle view of its own — and is the live proof that
            // the seat walk landed on a real player seat.
            const bool recognised = Halo3SeatFlagsLookLikePlayerSeat(kWarthogDriver) &&
                                    Halo3SeatFlagsLookLikePlayerSeat(kWarthogPassenger) &&
                                    Halo3SeatFlagsLookLikePlayerSeat(kMachinegunTurret) &&
                                    !Halo3SeatFlagsLookLikePlayerSeat(0u) &&
                                    // A record that already reads first person is refused, so a
                                    // mis-walked seat can never be "restored" to a value we
                                    // invented.
                                    !Halo3SeatFlagsLookLikePlayerSeat(Halo3FirstPersonSeatFlags(kWarthogDriver));
            // Exactly one bit changes, and only that bit. The passenger keeps
            // `allows weapons` (5) and `third person on enter` (6); the gunner
            // keeps `first person camera slaved to gun` (7) — the reference mod
            // clears more, and this candidate deliberately does not.
            const bool onlyBit4 = Halo3FirstPersonSeatFlags(kWarthogDriver) == 0x40004u &&
                                  Halo3FirstPersonSeatFlags(kWarthogPassenger) == 0x1060u &&
                                  Halo3FirstPersonSeatFlags(kMachinegunTurret) == 0x5500008u;
            // Restoring is the exact inverse for every seat in the table, so
            // seat exit cannot leave a vehicle permanently altered.
            bool reversible = true;
            for (uint32_t flags : {kWarthogDriver, kWarthogPassenger, kMachinegunTurret})
                reversible = reversible && (Halo3FirstPersonSeatFlags(flags) | kHalo3SeatThirdPersonCameraBit) == flags;
            Check(recognised && onlyBit4 && reversible, "The first-person seat flag clears only `third person camera` "
                                                        "on a seat proven to have it, and restores exactly");
        }

        // Hand origin: the seat carries the arms, the head never does.
        {
            const float camera[3] = {10.0f, 20.0f, 30.0f};
            const float body[3] = {1.0f, 2.0f, 3.0f};
            float origin[3] = {camera[0], camera[1], camera[2]};
            const bool takesBody = Halo3SelectHandOrigin(true, true, body, origin) && origin[0] == body[0] &&
                                   origin[1] == body[1] && origin[2] == body[2];

            // Every refusal must leave the caller's origin byte-for-byte alone,
            // so the fallback is exactly the head-anchored behavior this
            // replaces and never an invented position.
            bool untouched = true;
            const float nan3[3] = {1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f};
            const float inf3[3] = {std::numeric_limits<float>::infinity(), 2.0f, 3.0f};
            struct Refusal
            {
                bool follow;
                bool valid;
                const float* src;
            };
            const Refusal refusals[] = {
                {false, true, body},                       // feature off
                {true, false, body},                       // on foot / no seat sample this frame
                {false, false, body},                      // both
                {true, true, nan3},                        // torn or unfinished sample
                {true, true, inf3}, {true, true, nullptr}, // no publication at all
            };
            for (const Refusal& r : refusals)
            {
                float kept[3] = {camera[0], camera[1], camera[2]};
                if (Halo3SelectHandOrigin(r.follow, r.valid, r.src, kept))
                    untouched = false;
                if (kept[0] != camera[0] || kept[1] != camera[1] || kept[2] != camera[2])
                    untouched = false;
            }
            Check(takesBody && untouched, "The vehicle hand origin takes the rigid seat placement when "
                                          "it is published and finite, and otherwise leaves the camera "
                                          "origin exactly as it was");
        }

        // Virtual wheel: two gripped hands, the tilt of the line between
        // them, read in the head's own heading plane.
        {
            Halo3Wheel wheel;
            const float level[2][3] = {{-0.20f, 1.00f, -0.40f}, {0.20f, 1.00f, -0.40f}};
            auto click = [&](uint64_t at)
            {
                Halo3UpdateWheelToggle(wheel, true, 0.9f, 0.9f, at);
                return Halo3UpdateWheelToggle(wheel, true, 0.0f, 0.0f, at + 5);
            };
            // A single click is not a toggle, and neither is a slow pair.
            const bool singleIgnored = !click(1000) && !wheel.engaged;
            const bool slowPairIgnored = !click(3000) && !wheel.engaged;
            // Two clicks inside the window take the wheel; two more let go.
            const bool takes = click(3100) && wheel.engaged;
            const bool releases = !click(3300) && click(3400) && !wheel.engaged && wheel.steer == 0.0f;
            // Squeezing ONE grip is never the gesture, so it is never
            // swallowed — that is the dismount.
            Halo3Wheel lone;
            Halo3UpdateWheelToggle(lone, true, 0.0f, 0.9f, 10);
            const bool loneGripFree = !Halo3WheelSwallowsGrips(lone) && !lone.engaged;
            // Both grips down IS the gesture, so those buttons are withheld
            // for exactly that window — including the very first click, before
            // anything has toggled.
            Halo3Wheel gesture;
            Halo3UpdateWheelToggle(gesture, true, 0.9f, 0.9f, 20);
            const bool gestureSwallows = Halo3WheelSwallowsGrips(gesture);
            Halo3UpdateWheelToggle(gesture, true, 0.0f, 0.0f, 25);
            const bool freedOnRelease = !Halo3WheelSwallowsGrips(gesture);
            // Leaving the seat drops the wheel and frees the grips.
            Halo3UpdateWheelToggle(gesture, false, 0.9f, 0.9f, 30);
            const bool dropsWhenUnavailable = !gesture.engaged && !Halo3WheelSwallowsGrips(gesture);

            // Steering. Take the wheel with a real double-click, then read
            // hand poses only — nothing is squeezed while driving.
            Halo3Wheel driving;
            Halo3UpdateWheelToggle(driving, true, 0.9f, 0.9f, 100);
            Halo3UpdateWheelToggle(driving, true, 0.0f, 0.0f, 105);
            Halo3UpdateWheelToggle(driving, true, 0.9f, 0.9f, 200);
            const bool tookIt = Halo3UpdateWheelToggle(driving, true, 0.0f, 0.0f, 205) && driving.engaged;
            const bool centred =
                Halo3ComputeWheelSteer(driving, level[0], level[1], 0.0f, 75.0f, 6.0f) && driving.steer == 0.0f;
            // Right hand 45 deg up = counter-clockwise = steer LEFT.
            const float up45[3] = {-0.20f + 0.28284f, 1.0f + 0.28284f, -0.40f};
            Halo3ComputeWheelSteer(driving, level[0], up45, 0.0f, 75.0f, 6.0f);
            const float expected = -(45.0f - 6.0f) / (75.0f - 6.0f);
            const bool steersLeft = std::fabs(driving.steer - expected) < 2e-3f;
            // Mirror image steers right by the same amount.
            const float down45[3] = {-0.20f + 0.28284f, 1.0f - 0.28284f, -0.40f};
            Halo3ComputeWheelSteer(driving, level[0], down45, 0.0f, 75.0f, 6.0f);
            const bool steersRight = std::fabs(driving.steer + expected) < 2e-3f;
            // Past full lock saturates instead of wrapping.
            const float over[3] = {-0.20f + 0.02f, 1.0f + 0.40f, -0.40f};
            Halo3ComputeWheelSteer(driving, level[0], over, 0.0f, 75.0f, 6.0f);
            const bool saturates = std::fabs(driving.steer + 1.0f) < 1e-4f;
            // The same physical wheel, with the player turned 90 degrees in
            // the room, reads exactly the same.
            const float leftTurned[3] = {0.0f, 1.0f, -0.20f};
            const float rightTurned[3] = {0.0f, 1.0f + 0.28284f, -0.20f + 0.28284f};
            Halo3ComputeWheelSteer(driving, leftTurned, rightTurned, 1.5707963f, 75.0f, 6.0f);
            const bool headRelative = std::fabs(driving.steer - expected) < 2e-3f;
            // Hands dropped into a lap must read as straight ahead. Holding
            // the last steer would keep the vehicle turning with nobody
            // driving — a hold could get away with it, a toggle cannot.
            const float together[3] = {0.0f, 1.05f, -0.15f};
            const bool straightOnCollapse =
                Halo3ComputeWheelSteer(driving, leftTurned, together, 1.5707963f, 75.0f, 6.0f) && driving.steer == 0.0f;
            // Not taken, or non-finite input: the stick steers instead.
            Halo3Wheel idleWheel;
            const float nanHand[3] = {std::nanf(""), 1.0f, 0.0f};
            const bool failsOpen = !Halo3ComputeWheelSteer(idleWheel, level[0], level[1], 0.0f, 75.0f, 6.0f) &&
                                   !Halo3ComputeWheelSteer(driving, level[0], nanHand, 0.0f, 75.0f, 6.0f) &&
                                   driving.steer == 0.0f;

            Check(singleIgnored && slowPairIgnored && takes && releases && loneGripFree && gestureSwallows &&
                      freedOnRelease && dropsWhenUnavailable && tookIt && centred && steersLeft && steersRight &&
                      saturates && headRelative && straightOnCollapse && failsOpen,
                "Double-clicking both grips toggles the wheel, a lone grip is "
                "never swallowed so the dismount survives, and the wheel "
                "steers from the hand line in the head's own plane");
        }

        // Mode refinement: only a proven seated state upgrades Gameplay;
        // Turret requires the measured physics type 5 exactly; an unproven
        // type stays a plain Vehicle; Unknown and OnFoot never upgrade.
        Check(Halo3ClassifyGameplayUpgrade(Halo3VehicleState::Unknown, 5) == 0 &&
                  Halo3ClassifyGameplayUpgrade(Halo3VehicleState::OnFoot, 1) == 0 &&
                  Halo3ClassifyGameplayUpgrade(Halo3VehicleState::Vehicle, 1) == 1 &&
                  Halo3ClassifyGameplayUpgrade(Halo3VehicleState::Vehicle, 3) == 1 &&
                  Halo3ClassifyGameplayUpgrade(Halo3VehicleState::Vehicle, kHalo3VehicleTypeTurret) == 2 &&
                  Halo3ClassifyGameplayUpgrade(Halo3VehicleState::Vehicle, -1) == 1,
            "Gameplay upgrades to Vehicle/Turret only on proven seated state");
    }

    CheckUnit(246);
    {
        const Halo3MeleeResponse authored{0xff, 11, 12, 13, 14};
        auto response = Halo3SelectPhysicalMeleeResponse(90, true, 0xff, 20, 21, authored, false);
        Check(response.material == 63 && response.damage == 11 && response.effect == 12 && response.clashDamage == 13 &&
                  response.clashEffect == 14,
            "H3 secondary melee preserves its own authored response and native material mask");
        response = Halo3SelectPhysicalMeleeResponse(90, true, 7, 20, 21, {}, false);
        Check(response.damage == 20 && response.effect == 21 && response.clashDamage == UINT32_MAX,
            "H3 missing authored strike responses fall back to weapon defaults");
        response = Halo3SelectPhysicalMeleeResponse(90, true, 7, UINT32_MAX, 21, {}, false);
        Check(
            response.damage == 90 && response.effect == 21, "H3 missing weapon damage uses its unit's native fallback");
        response = Halo3SelectPhysicalMeleeResponse(90, false, 7, 20, 21, authored, true);
        Check(response.material == 3 && response.damage == 90 && response.effect == UINT32_MAX &&
                  response.clashDamage == UINT32_MAX && response.clashEffect == UINT32_MAX,
            "H3 bare hands cannot inherit the other weapon's effect or clash damage");
        response = Halo3SelectPhysicalMeleeResponse(90, true, 7, 20, 21, authored, true);
        Check(response.damage == 11 && response.effect == 12 && response.clashDamage == UINT32_MAX &&
                  response.clashEffect == UINT32_MAX,
            "H3 charged response preserves the native omission of clash responses");
    }
    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core h3 odst ce tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
