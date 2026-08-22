#pragma once

#include "audio.hpp"
#include "model.hpp"

#include <SDL.h>
#include <array>
#include <string>

namespace fms
{

    enum class ProjectRequestKind : std::uint8_t
    {
        // Request a new project without mutating the current session. The host
        // first preserves recoverable work, then installs defaults and calls
        // projectStarted() to establish the hard undo/path boundary.
        New,
        Open,
        SaveAs
    };

    struct ProjectRequest
    {
        ProjectRequestKind kind = ProjectRequestKind::Open;
        std::string path;
    };

    struct AdvancedFmTopologyEdge
    {
        std::uint8_t source = 0;
        std::uint8_t destination = 0;

        bool operator==(const AdvancedFmTopologyEdge &) const = default;
    };

    struct AdvancedFmAlgorithmTopology
    {
        std::array<AdvancedFmTopologyEdge, 4> modulationEdges{};
        std::uint8_t modulationEdgeCount = 0;
        std::uint8_t carrierMask = 0;

        bool operator==(const AdvancedFmAlgorithmTopology &) const = default;
    };

    // Declarative routing used by the Basic Synth diagram. Operators are
    // zero-based; an edge source modulates its destination and carrier bits
    // identify operators mixed into the final output.
    const AdvancedFmAlgorithmTopology &
    advancedFmAlgorithmTopology(AdvancedFmAlgorithm algorithm);

    class UiController
    {
    public:
        UiController(SharedState &state, AudioEngine &audio);
        ~UiController();
        UiController(const UiController &) = delete;
        UiController &operator=(const UiController &) = delete;

        // Returns false when the application should close.
        bool handleEvent(const SDL_Event &event);
        void update(double deltaSeconds);
        void render(SDL_Renderer *renderer, int width, int height);
        bool consumeSaveRequest();
        bool consumeProjectRequest(ProjectRequest &request);
        bool isDirty() const;
        void markSaved();
        void setProjectPath(const std::string &path);
        // Attach a freshly created default session to its new project path.
        // This is a hard undo boundary and intentionally starts dirty.
        void projectStarted(const std::string &path);
        void projectLoaded(const std::string &path);
        void showToast(const std::string &message, bool error = false);

    private:
        struct Impl;
        Impl *impl_ = nullptr;
    };

} // namespace fms
