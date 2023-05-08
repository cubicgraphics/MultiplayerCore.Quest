#include "Objects/MpLevelLoader.hpp"
#include "Utilities.hpp"
#include "Utils/ExtraSongData.hpp"
#include "lapiz/shared/utilities/MainThreadScheduler.hpp"
#include "logging.hpp"

#include "GlobalNamespace/PreviewDifficultyBeatmap.hpp"
#include "GlobalNamespace/IPreviewBeatmapLevel.hpp"
#include "GlobalNamespace/IConnectedPlayer.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "System/Threading/CancellationTokenSource.hpp"

DEFINE_TYPE(MultiplayerCore::Objects, MpLevelLoader);

// Accessing "private" method from pinkcore
namespace RequirementUtils {
    bool GetRequirementInstalled(std::string requirement);
}

namespace MultiplayerCore::Objects {
    void MpLevelLoader::ctor(GlobalNamespace::IMultiplayerSessionManager* sessionManager, MpLevelDownloader* levelDownloader, GlobalNamespace::NetworkPlayerEntitlementChecker* entitlementChecker, GlobalNamespace::IMenuRpcManager* rpcManager) {
        INVOKE_CTOR();
        INVOKE_BASE_CTOR(classof(GlobalNamespace::MultiplayerLevelLoader*));

        _sessionManager = sessionManager;
        _levelDownloader = levelDownloader;
        _entitlementChecker = il2cpp_utils::try_cast<MpEntitlementChecker>(entitlementChecker).value_or(nullptr);
        _rpcManager = rpcManager;
    }

    void MpLevelLoader::LoadLevel_override(GlobalNamespace::ILevelGameplaySetupData* gameplaySetupData, float initialStartTime) {
        // TODO: null check anyone?
        std::string levelId(gameplaySetupData->get_beatmapLevel()->get_beatmapLevel()->get_levelID());
        auto levelHash = Utilities::HashForLevelId(levelId);
        DEBUG("Loading Level {}", levelHash);
        LoadLevel(gameplaySetupData, initialStartTime);
        if (!levelHash.empty() && !RuntimeSongLoader::API::GetLevelByHash(levelHash).has_value())
            getBeatmapLevelResultTask = StartDownloadBeatmapLevelAsyncTask(levelId, getBeatmapCancellationTokenSource->get_Token());
    }

    void MpLevelLoader::Tick_override() {
        using MultiplayerBeatmapLoaderState = GlobalNamespace::MultiplayerLevelLoader::MultiplayerBeatmapLoaderState;

        auto beatmap = gameplaySetupData ? gameplaySetupData->get_beatmapLevel() : nullptr;
        auto beatmapLevel = beatmap ? beatmap->get_beatmapLevel() : nullptr;
        auto levelId = beatmapLevel ? beatmapLevel->get_levelID() : nullptr;

        if (Il2CppString::IsNullOrEmpty(levelId)) {
            GlobalNamespace::MultiplayerLevelLoader::Tick();
            return;
        }

        switch (loaderState) {
            case MultiplayerBeatmapLoaderState::LoadingBeatmap: {
                GlobalNamespace::MultiplayerLevelLoader::Tick();
                if (loaderState == MultiplayerBeatmapLoaderState::WaitingForCountdown) {
                    _rpcManager->SetIsEntitledToLevel(levelId, GlobalNamespace::EntitlementsStatus::Ok);
                    DEBUG("Loaded level {}", levelId);
                    auto hash = Utilities::HashForLevelId(levelId);
                    if (!hash.empty()) {
                        auto extraSongData = Utils::ExtraSongData::FromLevelHash(hash);
                        if (extraSongData.has_value()) {
                            const auto& difficulties = extraSongData->difficulties;

                            auto bmDiff = beatmap->get_beatmapDifficulty();
                            auto ch = beatmap->get_beatmapCharacteristic()->get_name();
                            auto diff = std::find_if(difficulties.begin(), difficulties.end(), [bmDiff, ch](const auto& x){
                                return x.difficulty == bmDiff && x.beatmapCharacteristicName == ch;
                            });

                            if (diff != difficulties.end() && diff->additionalDifficultyData.has_value()) {
                                for (const auto& req : diff->additionalDifficultyData->requirements) {
                                    if (!RequirementUtils::GetRequirementInstalled(req)) {
                                        difficultyBeatmap = nullptr;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            } break;
            case MultiplayerBeatmapLoaderState::WaitingForCountdown: {
                if (_sessionManager->get_syncTime() >= startTime) {
                    bool allFinished = true;
                    int pCount = _sessionManager->get_connectedPlayerCount();
                    for (std::size_t i = 0; i < pCount; i++) {
                        auto p = _sessionManager->GetConnectedPlayer(i);
                        bool hasEntitlement = _entitlementChecker->GetUserEntitlementStatusWithoutRequest(p->get_userId(), levelId) == GlobalNamespace::EntitlementsStatus::Ok;
                        bool in_gameplay = p->HasState("in_gameplay");

                        if (!(hasEntitlement || in_gameplay)) {
                            allFinished = false;
                            break;
                        }
                    }

                    if (allFinished) {
                        DEBUG("All players finished loading");
                        GlobalNamespace::MultiplayerLevelLoader::Tick();
                    }
                }
            } break;
            default:
                GlobalNamespace::MultiplayerLevelLoader::Tick();
                break;
        }
    }

    System::Threading::Tasks::Task_1<GlobalNamespace::BeatmapLevelsModel::GetBeatmapLevelResult>* MpLevelLoader::StartDownloadBeatmapLevelAsyncTask(std::string levelId, System::Threading::CancellationToken cancellationToken) {
        auto task = System::Threading::Tasks::Task_1<GlobalNamespace::BeatmapLevelsModel::GetBeatmapLevelResult>::New_ctor(0);
        task->m_stateFlags = System::Threading::Tasks::Task::TASK_STATE_STARTED;

        std::thread([this, levelId, task, cancellationToken](){
            _levelDownloader->TryDownloadLevelAsync(levelId, std::bind(&MpLevelLoader::Report, this, std::placeholders::_1)).wait();

            std::optional<GlobalNamespace::IPreviewBeatmapLevel*> getPreviewBeatmapLevelResult;
            Lapiz::Utilities::MainThreadScheduler::Schedule([&getPreviewBeatmapLevelResult, beatmapLevelsModel = beatmapLevelsModel, levelId](){
                getPreviewBeatmapLevelResult = beatmapLevelsModel->GetLevelPreviewForLevelId(levelId);
                DEBUG("Got level {}", fmt::ptr(getPreviewBeatmapLevelResult.value()));
            });
            while(!getPreviewBeatmapLevelResult.has_value()) std::this_thread::yield();
            gameplaySetupData->get_beatmapLevel()->beatmapLevel = getPreviewBeatmapLevelResult.value();

            std::optional<System::Threading::Tasks::Task_1<GlobalNamespace::BeatmapLevelsModel::GetBeatmapLevelResult>*> getTask;
            Lapiz::Utilities::MainThreadScheduler::Schedule([&getTask, beatmapLevelsModel = beatmapLevelsModel, levelId, cancellationToken](){
                getTask = beatmapLevelsModel->GetBeatmapLevelAsync(levelId, cancellationToken);
                DEBUG("Got task {}", fmt::ptr(getTask.value()));
            });
            while(!getTask.has_value() || !getTask.value()->get_IsCompleted()) std::this_thread::yield();
            auto result = getTask.value()->get_Result();

            DEBUG("GetBeatmapLevelAsync result isError: {}", result.isError);
            task->TrySetResult(result);
            task->m_stateFlags = System::Threading::Tasks::Task::TASK_STATE_RAN_TO_COMPLETION;
        }).detach();

        return task;
    }

    void MpLevelLoader::Report(double progress) {
        progressUpdated.invoke(progress);
    }
}
