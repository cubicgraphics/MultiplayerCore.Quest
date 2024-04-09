#include "Objects/MpLevelLoader.hpp"
#include "Utils/ExtraSongData.hpp"
#include "lapiz/shared/utilities/MainThreadScheduler.hpp"
#include "songcore/shared/SongLoader/RuntimeSongLoader.hpp"
#include "logging.hpp"
#include "tasks.hpp"
#include "Utilities.hpp"

#include "GlobalNamespace/IConnectedPlayer.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/BeatmapLevelsModel.hpp"
#include "System/Threading/CancellationTokenSource.hpp"
#include "System/Collections/Generic/IReadOnlyList_1.hpp"
#include "System/Collections/Generic/IReadOnlyCollection_1.hpp"
#include "System/Action_2.hpp"
#include <type_traits>

DEFINE_TYPE(MultiplayerCore::Objects, MpLevelLoader);

namespace MultiplayerCore::Objects {
    void MpLevelLoader::ctor(SongCore::SongLoader::RuntimeSongLoader* runtimeSongLoader, SongCore::Capabilities* capabilities, GlobalNamespace::IMultiplayerSessionManager* sessionManager, MpLevelDownloader* levelDownloader, GlobalNamespace::NetworkPlayerEntitlementChecker* entitlementChecker, GlobalNamespace::IMenuRpcManager* rpcManager) {
        INVOKE_CTOR();
        GlobalNamespace::MultiplayerLevelLoader::_ctor();

        _runtimeSongLoader = runtimeSongLoader;
        _capabilities = capabilities;
        _sessionManager = sessionManager;
        _levelDownloader = levelDownloader;
        _entitlementChecker = il2cpp_utils::try_cast<MpEntitlementChecker>(entitlementChecker).value_or(nullptr);
        _rpcManager = rpcManager;
    }

    void MpLevelLoader::LoadLevel_override(GlobalNamespace::ILevelGameplaySetupData* gameplaySetupData, long initialStartTime) {
        using Base = GlobalNamespace::MultiplayerLevelLoader;
        auto key = gameplaySetupData->beatmapKey;
        std::string levelId(key.levelId);
        std::string levelHash(!levelId.empty() ? HashFromLevelID(std::string_view(levelId)) : "");

        DEBUG("Loading Level '{}'", levelHash.empty() ? levelId : levelHash);
        Base::LoadLevel(gameplaySetupData, initialStartTime);
        if (levelHash.empty()) {
            DEBUG("Ignoring level (not a custom level hash): {}", levelId);
            return;
        }

        bool downloadNeeded = _runtimeSongLoader->GetLevelByHash(levelHash) == nullptr;

        if (downloadNeeded) {
            _getBeatmapLevelResultTask = StartDownloadBeatmapLevelAsyncTask(levelId, _getBeatmapCancellationTokenSource->Token);
        }
    }

    template<typename T, typename U>
    requires(std::is_invocable_r_v<bool, U, T>)
    bool All(System::Collections::Generic::IReadOnlyList_1<T>* list, U predicate) {
        auto count = list->i___System__Collections__Generic__IReadOnlyCollection_1_T_()->Count;
        if (count == 0) WARNING("0 items checked!");
        for (int i = 0; i < count; i++) {
            auto item = list->get_Item(i);
            if (!predicate(item)) return false;
        }

        return true;
    }

    void MpLevelLoader::Tick_override() {
        using MultiplayerBeatmapLoaderState = GlobalNamespace::MultiplayerLevelLoader::MultiplayerBeatmapLoaderState;
        using Base = GlobalNamespace::MultiplayerLevelLoader;
        if (_loaderState == MultiplayerBeatmapLoaderState::NotLoading) return;

        auto beatmapKey = _gameplaySetupData->beatmapKey;
        auto levelId = beatmapKey.levelId;

        if (_loaderState == MultiplayerBeatmapLoaderState::WaitingForCountdown) {
            // if start time is greater than sync time we're still in countdown, don't try to do anything.
            if (_startTime > _multiplayerSessionManager->syncTime) return;

            // we are past countdown, now check all players
            auto allPlayersReady = All(_multiplayerSessionManager->connectedPlayers, [this, levelId](auto p){
                return
                _entitlementChecker->GetKnownEntitlement(p->userId, levelId) == GlobalNamespace::EntitlementsStatus::Ok || // has level
                p->HasState("in_gameplay") || // already playing
                p->HasState("backgrounded") || // not actively in game
                !p->HasState("wants_to_play_next_level"); // doesn't want to play (spectator)
            });

            if (!allPlayersReady) return;
            DEBUG("All players finished loading");
            Base::Tick();
            return;
        }

        Base::Tick();

        auto loadDidFinish = (_loaderState == MultiplayerBeatmapLoaderState::WaitingForCountdown);
        if (!loadDidFinish) return;

        _rpcManager->SetIsEntitledToLevel(levelId, GlobalNamespace::EntitlementsStatus::Ok);
        DEBUG("Loaded Level {}", levelId);
        UnloadLevelIfRequirementsNotMet();
    }

    void MpLevelLoader::UnloadLevelIfRequirementsNotMet() {
        auto beatmapKey = _gameplaySetupData->beatmapKey;
        auto levelId = beatmapKey.levelId;
        auto levelHash = HashFromLevelID(levelId);
        // not a custom level
        if (levelHash.empty()) return;

        auto level = _runtimeSongLoader->GetLevelByHash(levelHash);
        // level not loaded or savedata not loaded
        if (!level || !level->standardLevelInfoSaveData) return;

        auto diffDataOpt = level->standardLevelInfoSaveData->TryGetCharacteristicAndDifficulty(beatmapKey.beatmapCharacteristic->serializedName, beatmapKey.difficulty);
        if (diffDataOpt.has_value()) return;

        auto& diffData = diffDataOpt->get();
        bool requirementsMet = true;
        for (auto& requirement : diffData.requirements) {
            if (_capabilities->IsCapabilityRegistered(requirement)) continue;
            WARNING("Level requirements not met: {}", requirement);
            requirementsMet = false;
        }

        if (requirementsMet) return;

        DEBUG("Level will be unloaded due to unmet requirements");
        // TODO: should this also make _rpcManager send NotOwned?
        _beatmapLevelData = nullptr;
    }

    System::Threading::Tasks::Task_1<GlobalNamespace::LoadBeatmapLevelDataResult>* MpLevelLoader::StartDownloadBeatmapLevelAsyncTask(std::string levelId, System::Threading::CancellationToken cancellationToken) {
        return StartTask<GlobalNamespace::LoadBeatmapLevelDataResult>([this, levelId, cancellationToken](CancellationToken token) -> GlobalNamespace::LoadBeatmapLevelDataResult {
            static auto Error = GlobalNamespace::LoadBeatmapLevelDataResult(true, nullptr);
            auto success = _levelDownloader->TryDownloadLevelAsync(levelId, std::bind(&MpLevelLoader::Report, this, std::placeholders::_1)).get();
            if (!success) return Error;
            auto level = _runtimeSongLoader->GetLevelByLevelID(levelId);
            if (!level) return Error;
            if (!level->beatmapLevelData) return Error;
            return GlobalNamespace::LoadBeatmapLevelDataResult::Success(level->beatmapLevelData);
        }, std::forward<CancellationToken>(cancellationToken));
    }

    void MpLevelLoader::Report(double progress) {
        progressUpdated.invoke(progress);
    }
}
