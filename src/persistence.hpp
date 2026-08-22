#pragma once

#include "model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fms
{

    struct ProjectFileTarget
    {
        std::string path;
        // False protects a file that existed but could not be decoded/read at
        // startup. Such a path is never passed to replacement saving.
        bool readableOrMissing = true;
        // False selects race-free create-new semantics for the first save.
        bool exists = false;
    };

    enum class ProjectSaveResult
    {
        Saved,
        Recovered,
    };

    std::string defaultSavePath();
    // Returns a sanitized, currently-unused path below the XDG projects
    // directory. Existing names receive a numeric suffix instead of being
    // selected for silent replacement.
    std::string projectPathForName(std::string_view name);
    std::vector<std::string> recentProjectPaths();
    bool saveState(const AppState &state, const std::string &path, std::string &error);
    // Create a new project atomically. If path already exists it is preserved
    // and the operation fails, closing the check/rename race in Save As.
    bool saveStateNew(const AppState &state, const std::string &path, std::string &error);
    // Save to the active project target. When recoveryOnFailure is true, an
    // unreadable/invalid/failing target is preserved and a unique managed
    // recovered-session project is attempted instead.
    bool saveProjectState(const AppState &state, ProjectFileTarget &target,
                          bool recoveryOnFailure, ProjectSaveResult &result,
                          std::string &error);
    // Transactionally prepare a New project. When preserveCurrent is true,
    // the supplied state is first saved/recovered; on any failure both state
    // and target remain the current project. Success installs fresh starter
    // data while retaining user preferences and assigns a unique untitled
    // create-new target.
    bool prepareNewProject(AppState &state, ProjectFileTarget &target,
                           bool preserveCurrent, ProjectSaveResult &preservationResult,
                           std::string &error);
    bool loadState(AppState &state, const std::string &path, std::string &error);

} // namespace fms
