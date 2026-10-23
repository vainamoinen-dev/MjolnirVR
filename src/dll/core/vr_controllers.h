// vr_controllers.h: what vr_controllers.cpp provides to the other VR units (controller actions, aim pose, two-hand and support grip, haptics), in namespace vr_core.
// Defined in vr_controllers.cpp.
#pragma once
#include "vr_internal.h"

namespace vr_core
{
    void AdvanceAimContinuityForPreparedFrame(bool padFresh, float dtSeconds) noexcept;
    void AdvanceTwoHandInputSmoothingForPreparedFrame(uint64_t preparedSerial, bool padFresh, float dtSeconds) noexcept;
    void AdvanceTwoHandLabForPreparedFrame(bool padFresh, float dtSeconds) noexcept;
    bool CaptureHeadPose(
        XrTime time, bool stockControllersFresh, virtual_stock::InverseNeckNeutralCaptureInput neutralInput);
    bool CaptureRightControllerPose(XrTime time);
    bool ComposePresentedAimForFrameThread(const XrQuaternionf& liveOrientation, uint64_t preparedSerial,
        bool stockHeadCoherent, bool supportEndpointUsedGrip, uint64_t labGeneration, bool virtualStockEnabled,
        XrQuaternionf& outPresented) noexcept;
    bool CreateControllerActions();
    void InvalidateTwoHandLabTemporal() noexcept;
    two_hand_lab::AgreementMode NormalizeTwoHandLabAgreement(int value) noexcept;
    two_hand_lab::AnchorMode NormalizeTwoHandLabAnchor(int value) noexcept;
    two_hand_lab::TemporalMode NormalizeTwoHandLabTemporal(int value) noexcept;
    void PublishTwoHandInputSmoothing(uint64_t serial, uint64_t contactSpaceEpoch, GameTitle title,
        uint32_t titleGeneration, bool leftHanded, bool supportEndpointUsedGrip, bool active,
        const two_hand_input_smoothing::Result& result) noexcept;
    void PublishTwoHandLabSettings(const two_hand_lab::Settings& settings) noexcept;
    bool ReadSupportGripRelationshipInternal(SupportGripRelationshipSnapshot& snapshot) noexcept;
    TwoHandLabSettingsSnapshot SnapshotTwoHandLabSettingsWithGeneration() noexcept;
    uint32_t WeaponGestureBinding(GameTitle title, vr_mapping::Action action, uint64_t now);
    // From virtual_stock_aim.inl (included in vr_core).
    ResolvedVirtualStockAimSettings CurrentResolvedVirtualStockAimSettings() noexcept;
    AimPoseInputs AimPoseInputsForProfile(const AimPoseInputs& base, VirtualStockTestProfile profile) noexcept;
    AimPoseInputs CurrentStockAimPoseInputs(bool rightValid, const XrPosef& right, bool leftValid, const XrPosef& left,
        bool coherentHeadValid, const XrVector3f& coherentHeadPosition, const XrQuaternionf& coherentHeadOrientation,
        bool supportGripPositionValid, const XrVector3f& supportGripPosition, bool primaryGripPositionValid,
        const XrVector3f& primaryGripPosition) noexcept;
} // namespace vr_core
