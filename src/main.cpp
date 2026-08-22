#include "audio.hpp"
#include "model.hpp"
#include "persistence.hpp"
#include "ui.hpp"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace
{

    struct Options
    {
        bool help = false;
        bool startPlaying = false;
        bool noAudio = false;
        bool audioSmoke = false;
        double runForSeconds = 0.0;
        std::string screenshot;
        std::string savePath;
        std::string error;
    };

    Options parseOptions(int argc, char **argv)
    {
        Options options;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
                options.help = true;
            else if (std::strcmp(argv[i], "--play") == 0)
                options.startPlaying = true;
            else if (std::strcmp(argv[i], "--no-audio") == 0)
                options.noAudio = true;
            else if (std::strcmp(argv[i], "--audio-smoke") == 0)
            {
                options.audioSmoke = true;
            }
            else if (std::strcmp(argv[i], "--run-for") == 0)
            {
                if (i + 1 >= argc)
                {
                    options.error = "--run-for requires a number of seconds";
                    break;
                }
                char *end = nullptr;
                const double seconds = std::strtod(argv[++i], &end);
                if (end == argv[i] || *end != '\0' || !std::isfinite(seconds) || seconds < 0.0)
                {
                    options.error = "invalid --run-for value: " + std::string(argv[i]);
                    break;
                }
                options.runForSeconds = seconds;
            }
            else if (std::strcmp(argv[i], "--screenshot") == 0)
            {
                if (i + 1 >= argc)
                {
                    options.error = "--screenshot requires a file path";
                    break;
                }
                options.screenshot = argv[++i];
            }
            else if (std::strcmp(argv[i], "--save-path") == 0)
            {
                if (i + 1 >= argc)
                {
                    options.error = "--save-path requires a file path";
                    break;
                }
                options.savePath = argv[++i];
            }
            else
            {
                options.error = "unknown option: " + std::string(argv[i]);
                break;
            }
        }
        if (options.audioSmoke)
        {
            options.startPlaying = true;
            options.runForSeconds = 0.35;
        }
        return options;
    }

    void printUsage()
    {
        std::puts(
            "FMS Linux - native FM step sequencer\n\n"
            "Usage: fms-linux [options]\n"
            "  --play             start the transport on launch\n"
            "  --no-audio         disable audio initialization\n"
            "  --run-for SEC      close automatically after SEC seconds\n"
            "  --audio-smoke      run a short dummy-driver-friendly audio check\n"
            "  --screenshot FILE  render one frame to a BMP and exit\n"
            "  --save-path FILE   override the XDG save location\n"
            "  -h, --help         show this help");
    }

    bool saveScreenshot(SDL_Renderer *renderer, int width, int height, const std::string &path)
    {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!surface)
            return false;
        const bool ok = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                             surface->pixels, surface->pitch) == 0 &&
                        SDL_SaveBMP(surface, path.c_str()) == 0;
        SDL_FreeSurface(surface);
        return ok;
    }

    std::unique_ptr<fms::AppState> copyForSave(const fms::SharedState &shared)
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        return std::make_unique<fms::AppState>(shared.app);
    }

} // namespace

int main(int argc, char **argv)
{
    const Options options = parseOptions(argc, argv);
    if (!options.error.empty())
    {
        std::fprintf(stderr, "FMS: %s\n", options.error.c_str());
        printUsage();
        return 64;
    }
    if (options.help)
    {
        printUsage();
        return 0;
    }
    const std::uint32_t sdlFlags = SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER |
                                   (options.noAudio ? 0u : SDL_INIT_AUDIO);
    if (SDL_Init(sdlFlags) != 0)
    {
        std::fprintf(stderr, "FMS: SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window *window = SDL_CreateWindow(
        "FMS — native FM step sequencer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 760, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window)
    {
        std::fprintf(stderr, "FMS: could not create a window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 960, 570);

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
    {
        std::fprintf(stderr, "FMS: could not create a renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    fms::SharedState shared;
    shared.app = fms::makeDefaultState();
    std::string savePath = options.savePath.empty() ? fms::defaultSavePath() : options.savePath;
    std::string loadError;
    auto loaded = std::make_unique<fms::AppState>();
    const bool loadedProject = fms::loadState(*loaded, savePath, loadError);
    if (loadedProject)
        shared.app = std::move(*loaded);
    loaded.reset();
    fms::ProjectFileTarget project{savePath, loadError.empty(), loadedProject};

    fms::AudioEngine audio;
    const bool audioReady = options.noAudio ? false : audio.open(shared);
    auto ui = std::make_unique<fms::UiController>(shared, audio);
    ui->setProjectPath(savePath);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    if (!audioReady && !options.noAudio)
        ui->showToast("AUDIO OFFLINE - " + audio.error(), true);
    if (!loadError.empty())
    {
        std::fprintf(stderr, "FMS: %s; preserving the unreadable save file\n", loadError.c_str());
        ui->showToast("SAVE FILE ERROR - ORIGINAL PROTECTED", true);
    }
    if (options.startPlaying && audioReady)
        audio.setRunning(true);

    bool running = true;
    int exitCode = 0;
    const auto started = std::chrono::steady_clock::now();
    auto previous = std::chrono::steady_clock::now();
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
            else if (!ui->handleEvent(event))
                running = false;
        }

        const auto now = std::chrono::steady_clock::now();
        const double delta = std::chrono::duration<double>(now - previous).count();
        previous = now;
        ui->update(std::min(delta, 0.1));

        int width = 0;
        int height = 0;
        SDL_GetRendererOutputSize(renderer, &width, &height);
        ui->render(renderer, width, height);

        if (!options.screenshot.empty())
        {
            if (!saveScreenshot(renderer, width, height, options.screenshot))
            {
                std::fprintf(stderr, "FMS: screenshot failed: %s\n", SDL_GetError());
                exitCode = 3;
                running = false;
            }
            else
            {
                running = false;
            }
        }
        SDL_RenderPresent(renderer);

        if (ui->consumeSaveRequest())
        {
            std::string error;
            const auto saveSnapshot = copyForSave(shared);
            fms::ProjectSaveResult saveResult = fms::ProjectSaveResult::Saved;
            const bool saved = fms::saveProjectState(*saveSnapshot, project, true,
                                                     saveResult, error);
            const bool recovered = saveResult == fms::ProjectSaveResult::Recovered;
            if (saved)
            {
                savePath = project.path;
                ui->setProjectPath(savePath);
                ui->markSaved();
                if (recovered)
                    ui->showToast("ORIGINAL PRESERVED - RECOVERY COPY SAVED");
            }
            else
                ui->showToast("SAVE FAILED - " + error, true);
        }

        fms::ProjectRequest projectRequest;
        if (ui->consumeProjectRequest(projectRequest))
        {
            std::string error;
            if (projectRequest.kind == fms::ProjectRequestKind::New)
            {
                const bool preserveCurrent = ui->isDirty();
                audio.setRunning(false);
                audio.reset();
                auto newState = copyForSave(shared);
                fms::ProjectSaveResult saveResult = fms::ProjectSaveResult::Saved;
                const bool preserved = fms::prepareNewProject(
                    *newState, project, preserveCurrent, saveResult, error);
                const bool recovered = saveResult == fms::ProjectSaveResult::Recovered;
                if (!preserved)
                {
                    ui->showToast("NEW CANCELLED - SAVE FAILED - " + error, true);
                }
                else
                {
                    {
                        std::lock_guard<std::mutex> lock(shared.mutex);
                        shared.app = std::move(*newState);
                    }
                    savePath = project.path;
                    ui->projectStarted(savePath);
                    ui->showToast(recovered
                                      ? "OLD WORK RECOVERED - NEW PROJECT READY"
                                      : "NEW PROJECT - CTRL+SHIFT+S TO NAME");
                }
            }
            else if (projectRequest.kind == fms::ProjectRequestKind::SaveAs)
            {
                const auto saveSnapshot = copyForSave(shared);
                if (fms::saveStateNew(*saveSnapshot, projectRequest.path, error))
                {
                    savePath = projectRequest.path;
                    project = fms::ProjectFileTarget{savePath, true, true};
                    ui->setProjectPath(savePath);
                    ui->markSaved();
                }
                else
                {
                    ui->showToast("SAVE AS FAILED - " + error, true);
                }
            }
            else
            {
                bool preserved = true;
                if (ui->isDirty())
                {
                    audio.setRunning(false);
                    audio.reset();
                    const auto saveSnapshot = copyForSave(shared);
                    fms::ProjectSaveResult saveResult = fms::ProjectSaveResult::Saved;
                    preserved = fms::saveProjectState(*saveSnapshot, project, true,
                                                      saveResult, error);
                    const bool recovered =
                        saveResult == fms::ProjectSaveResult::Recovered;
                    if (preserved)
                    {
                        savePath = project.path;
                        ui->setProjectPath(savePath);
                        ui->markSaved();
                        if (recovered)
                            ui->showToast("CURRENT WORK RECOVERED BEFORE OPEN");
                    }
                    else
                    {
                        ui->showToast("OPEN CANCELLED - SAVE FAILED - " + error, true);
                    }
                }

                if (preserved)
                {
                    auto opened = std::make_unique<fms::AppState>();
                    if (fms::loadState(*opened, projectRequest.path, error))
                    {
                        audio.setRunning(false);
                        audio.reset();
                        {
                            std::lock_guard<std::mutex> lock(shared.mutex);
                            shared.app = std::move(*opened);
                        }
                        savePath = projectRequest.path;
                        project = fms::ProjectFileTarget{savePath, true, true};
                        ui->projectLoaded(savePath);
                    }
                    else
                    {
                        if (error.empty())
                            error = "project file was not found";
                        ui->showToast("OPEN FAILED - " + error, true);
                    }
                }
            }
        }

        if (options.runForSeconds > 0.0 &&
            std::chrono::duration<double>(now - started).count() >= options.runForSeconds)
        {
            running = false;
        }
    }

    if (options.audioSmoke)
    {
        const fms::TransportStatus status = audio.status();
        const float peak = std::max(std::abs(status.peakLeft), std::abs(status.peakRight));
        const bool playheadAdvanced = std::any_of(
            status.playheads.begin(), status.playheads.end(), [](int step)
            { return step >= 0; });
        if (!audioReady || status.renderedFrames == 0 ||
            !std::isfinite(status.peakLeft) || !std::isfinite(status.peakRight) ||
            peak <= 0.000001f || !status.running || !playheadAdvanced)
        {
            std::fprintf(stderr, "FMS: audio smoke test failed (%llu frames, peaks %.3f/%.3f)\n",
                         static_cast<unsigned long long>(status.renderedFrames),
                         static_cast<double>(status.peakLeft), static_cast<double>(status.peakRight));
            exitCode = 2;
        }
    }

    if (ui->isDirty() && !project.path.empty())
    {
        // Queued transport intents are ephemeral at shutdown. Invalidate them
        // before taking the final snapshot so the callback cannot mutate
        // SharedState after the bytes selected for autosave.
        audio.setRunning(false);
        audio.reset();
        std::string error;
        const auto saveSnapshot = copyForSave(shared);
        fms::ProjectSaveResult saveResult = fms::ProjectSaveResult::Saved;
        const bool saved = fms::saveProjectState(*saveSnapshot, project, true,
                                                 saveResult, error);
        const bool recovered = saveResult == fms::ProjectSaveResult::Recovered;
        if (!saved)
        {
            std::fprintf(stderr, "FMS: could not preserve unsaved changes: %s\n", error.c_str());
            if (exitCode == 0)
                exitCode = 4;
        }
        else if (recovered)
            std::fprintf(stderr, "FMS: original save target preserved; recovery saved to '%s'\n",
                         project.path.c_str());
    }
    ui.reset();
    audio.close();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
