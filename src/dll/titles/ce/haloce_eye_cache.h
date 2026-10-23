// halo_ce::EyeCache: the GPU textures that hold CE's completed left/right eye pair (plus the
// scope image and optional DLSS depth) between the game's render and the VR submit.
// Filled by haloce_stereo_core.cpp, read by vr.cpp; tested by tests/haloce_eye_cache_tests.cpp.
// Capture never allocates, logs, waits or calls native code.
#pragma once
#include <d3d11.h>
#include <atomic>
#include "../../../common/titles/ce/haloce_prepared_handoff.h"
#include "../../../common/titles/ce/haloce_classic_view_pair.h"
#include "../../../common/dlss_logic.h"

namespace halo_ce
{
    // Owned GPU resources use the same complete-pair rule as the existing titles.
    // Prepare/Reset are cold-only. Capture only issues a GPU copy: no resource
    // queries, AddRef/Release, allocation, logging, waiting, or native calls.
    // The native adapter must supply the ACTUAL source of CE's scoped transfer,
    // with a descriptor proven for that live resource. A wrapper address is not
    // a resource, and a descriptor inferred from the backbuffer is not proof.
    class EyeCache
    {
    public:
        struct Key
        {
            uint32_t generation{};
            uint64_t spaceEpoch{}, serial{}, resourceEpoch{};
            bool operator==(const Key&) const = default;
        };
        struct Completed
        {
            Key key;
            Tracking tracking;
            Cover covers[2];
            ID3D11Texture2D* eyes[2]{}; // borrowed until ReleaseCompleted
            // Classic look-through scope (third render pass) captured with this
            // pair, or null. Same descriptor and borrow as the eyes.
            ID3D11Texture2D* scope{};
            D3D11_TEXTURE2D_DESC descriptor{};
            uint64_t borrowId{};
            ID3D11ShaderResourceView* depthViews[2]{}; // same borrow as color
            D3D11_TEXTURE2D_DESC depthDescriptor{};
            dlss::CameraSample depthCameras[2]{};
            uint64_t depthHistoryEpoch{};
            // The tracking sample the scope image was rendered with (the eye pair's
            // own for a Classic scope; the monitor frame's for an Anniversary monitor image).
            Tracking scopeTracking;
        };
        EyeCache() = default;
        ~EyeCache(); // owner must retire all callbacks/borrows before destruction
        EyeCache(const EyeCache&) = delete;
        EyeCache& operator=(const EyeCache&) = delete;

        bool Prepare(ID3D11Device* device, ID3D11DeviceContext* context, const D3D11_TEXTURE2D_DESC& source,
            uint32_t generation, uint64_t resourceEpoch) noexcept;
        bool Reset() noexcept; // busy means retry on a later cold poll; never wait
        bool Begin(const PreparedReceipt& receipt, Key& key) noexcept;
        bool Begin(const ClassicViewPair& pair, Key& key) noexcept;
        bool Capture(Key key, int eye, ID3D11DeviceContext* context, ID3D11Resource* liveSource,
            const D3D11_TEXTURE2D_DESC& provenSource) noexcept;
        // Independent optional depth banks: a miss never changes color admission.
        bool PrepareDepth(ID3D11Device* device, ID3D11DeviceContext* context, const D3D11_TEXTURE2D_DESC& provenDepth,
            uint32_t generation) noexcept;
        bool CaptureDepth(Key key, int eye, ID3D11DeviceContext* context, ID3D11Resource* liveSource,
            const D3D11_TEXTURE2D_DESC& provenDepth, const dlss::CameraSample& camera, uint64_t historyEpoch) noexcept;
        // Optional late native HUD output: replace an already captured world pair
        // from its full-width, top/bottom packed surface before Finish. Refusal
        // retains that valid world pair. The adapter proves source lifetime/layout.
        bool CapturePacked(Key key, ID3D11DeviceContext* context, ID3D11Resource* liveSource,
            const D3D11_TEXTURE2D_DESC& provenPackedSource) noexcept;
        // Cold, lazy: the scope bank is allocated only once a scope is wanted
        // (never while the scope is off). Released with the eye banks.
        bool PrepareScope(ID3D11Device* device) noexcept;
        bool HasScopeBank() const noexcept { return scopeReady_.load(std::memory_order_acquire); }
        // Optional Classic scope image: only after both eyes of this key, before
        // Finish. Refusal never affects the eyes.
        bool CaptureScope(Key key, ID3D11DeviceContext* context, ID3D11Resource* liveSource,
            const D3D11_TEXTURE2D_DESC& provenSource) noexcept;
        bool Finish(Key key) noexcept; // call AFTER the native frame has returned
        // Anniversary monitor frame (tracking.monitorFrame): eye 0's image becomes the held
        // scope image; the completed eye pair is left as it was (shown again).
        bool FinishMonitor(Key key) noexcept;
        bool Drop(Key key) noexcept;
        bool AcquireCompleted(Key key, ID3D11DeviceContext* submissionContext, Completed& out) noexcept;
        bool ReleaseCompleted(uint64_t borrowId) noexcept;

    private:
        // One bounded CAS grants exclusive access. A contended operation declines
        // this frame; there is no spin, OS lock, or partial resource publication.
        std::atomic<uint64_t> use_{}; // 0 idle, 1 operation, >=2 unique submission borrow
        uint64_t lastBorrowId_{1};    // never reused, including across resource retirement
        ID3D11DeviceContext* context_{};
        ID3D11Texture2D* eyes_[2]{};
        // Rendering cannot overwrite the last coherent pair. A rejected or busy
        // frame retains its completed predecessor, with that predecessor's poses.
        ID3D11Texture2D* completedEyes_[2]{};
        // Scope bank, allocated cold with the eyes; swapped at Finish only when
        // captured for that exact key.
        ID3D11Texture2D *scope_{}, *completedScope_{};
        bool scopeCaptured_{}, completedScopeValid_{};
        bool monitorHeld_{}; // the held scope image is a monitor frame's
        Tracking completedScopeTracking_{};
        std::atomic<bool> scopeReady_{};
        D3D11_TEXTURE2D_DESC source_{}, cache_{};
        uint32_t generation_{};
        uint64_t resourceEpoch_{}, lastResourceEpoch_{}, lastSerial_{};
        Key key_{};
        Key completedKey_{};
        Tracking tracking_{};
        Tracking completedTracking_{};
        Cover covers_[2]{};
        Cover completedCovers_[2]{};
        unsigned mask_{};
        bool complete_{};
        ID3D11Texture2D *depth_[2]{}, *completedDepth_[2]{};
        ID3D11ShaderResourceView *depthViews_[2]{}, *completedDepthViews_[2]{};
        D3D11_TEXTURE2D_DESC depthSource_{}, depthDescriptor_{};
        dlss::CameraSample depthCameras_[2]{}, completedDepthCameras_[2]{};
        unsigned depthMask_{};
        bool completedDepthValid_{};
        uint64_t depthHistoryEpoch_{}, completedDepthHistoryEpoch_{};
        bool Enter() noexcept;
        void Leave() noexcept;
        void ClearFrame() noexcept;
        void ReleaseResources() noexcept;
        void ReleaseDepthResources() noexcept;
    };
}
