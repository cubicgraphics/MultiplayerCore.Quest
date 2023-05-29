#include "UI/MpDownloadedSongsGSM.hpp"
#include "assets.hpp"
#include "logging.hpp"

#include "bsml/shared/BSML.hpp"
#include "lapiz/shared/utilities/MainThreadScheduler.hpp"
#include "songloader/shared/API.hpp"

#include "GlobalNamespace/IMenuRpcManager.hpp"

#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"

DEFINE_TYPE(MultiplayerCore::UI, MpDownloadedSongsGSM);
DEFINE_TYPE(MultiplayerCore::UI, MpDownloadedSongCellInfo);

namespace MultiplayerCore::UI {
    bool cellIsSelected = false;
    MpDownloadedSongsGSM* MpDownloadedSongsGSM::_instance;
    std::vector<std::string> MpDownloadedSongsGSM::mapQueue;
    std::vector<std::string> MpDownloadedSongsGSM::downloadedSongs;

    void MpDownloadedSongsGSM::ctor() {
        INVOKE_CTOR();
    }

    // TODO: Add keep all and delete all option
    void MpDownloadedSongsGSM::DidActivate(bool firstActivation) {
        DEBUG("MpDownloadedSongsGSM::DidActivate");
        if (firstActivation) {
            _instance = this;
            BSML::parse_and_construct(IncludedAssets::DownloadedSongs_bsml, get_transform(), this);
        }

        // InsertCell("5E3AFB8269F6D06EE9D1839EBF413B947DC931FF");

        DEBUG("DownloadedSongsGSM::DidActivate");
        Refresh();
    }


    void MpDownloadedSongsGSM::OnEnable() {
        if (list && list->tableView) {
            if (!mapQueue.empty()) {
                InsertCell(mapQueue.back());
                mapQueue.pop_back();
            }

            Refresh();
        }
    }

    void MpDownloadedSongsGSM::AddToQueue(std::string hash) { mapQueue.push_back(hash); }

#pragma region Song list updating
    void MpDownloadedSongsGSM::InsertCell(std::string hash) {
        DEBUG("DownloadedSongsGSM::InsertCell");
        std::optional<GlobalNamespace::CustomPreviewBeatmapLevel*> levelOpt = RuntimeSongLoader::API::GetLevelByHash(hash);
        MpDownloadedSongsGSM* instance = get_instance();
        if (levelOpt.has_value() && instance) {
            GlobalNamespace::CustomPreviewBeatmapLevel* level = levelOpt.value();
            instance->lastDownloaded = level;
            INFO("Song with levelId '{}' added to list", level->get_levelID());

            if (level->coverImage) { // if cover image already loaded
                instance->AddCell(level->coverImage, level);
            } else { // still need to load it
                std::thread([level](){
                    auto t = level->GetCoverImageAsync(System::Threading::CancellationToken::get_None());
                    while (!t->get_IsCompleted()) std::this_thread::yield();

                    auto instance = get_instance();
                    if (instance && instance->m_CachedPtr.m_value) {
                        instance->AddCell(t->get_Result(), level);
                    }
                }).detach();
            }
        } else if (levelOpt.has_value()) {
            DEBUG("DownloadedSongsGSM::InsertCell: Instance is nullptr, putting hash '{}' in queue", hash);
            mapQueue.push_back(hash);
        } else {
            ERROR("Song with hash '{}' not found, was it already deleted?", hash);
        }
    }

    void MpDownloadedSongsGSM::Update() {
        if (needRefresh) {
            Refresh();
            needRefresh = false;
        }

        if (!mapQueue.empty()) {
            InsertCell(mapQueue.front());
            mapQueue.pop_back();
        }
    }

    void MpDownloadedSongsGSM::Refresh() {
        DEBUG("DownloadedSongsGSM::Refresh");

        if (!get_isActiveAndEnabled()) {
            DEBUG("Enqueueing refresh for later");
            needRefresh = true;
            return;
        }

        list->tableView->ClearSelection();
        list->tableView->ReloadData();

        list->tableView->RefreshCellsContent();
    }

    void MpDownloadedSongsGSM::AddCell(UnityEngine::Sprite* coverImageSprite, GlobalNamespace::CustomPreviewBeatmapLevel* level) {
        if (level) { // we have cover & level
            auto songName = level->get_songName();
            auto authorName = level->get_songAuthorName();
            if (!authorName) authorName = level->get_levelAuthorName();

            auto cover = coverImageSprite ? coverImageSprite : level->get_defaultCoverImage();

            _data->Add(MpDownloadedSongCellInfo::construct(
                songName ? songName : "Error: songName null",
                level->get_levelID(),
                authorName ? fmt::format("[{}]", authorName) : "",
                cover
            ));
        } else {
            ERROR("Cover & level are null!");
        }

        needRefresh = true;
    }

    std::string MpDownloadedSongsGSM::LevelIdAtIndex(int idx) {
        return idx >= 0 && idx < _data.size() ? _data[idx]->levelId : "";
    }
#pragma endregion // Song List updating

#pragma region UI events
    void MpDownloadedSongsGSM::SelectCell(HMUI::TableView* tableView, int idx)
    {
        DEBUG("Cell with idx {} selected", idx);
        cellIsSelected = true;
        selectedIdx = idx;
        modal->Show();
    }

    void MpDownloadedSongsGSM::Keep() {
        downloadedSongs.erase(downloadedSongs.begin() + selectedIdx);
        list->tableView->ClearSelection();
        _data->RemoveAt(selectedIdx);
        Refresh();
        modal->Hide();
        cellIsSelected = false;
    }

    // TODO: Add index check, check if index is out of bounds
    void MpDownloadedSongsGSM::Delete() {
        try {
            needSongRefresh = false;
            auto level = RuntimeSongLoader::API::GetLevelById(LevelIdAtIndex(selectedIdx));

            if (level.has_value()) {
                auto songPath = std::string(level.value()->get_customLevelPath());
                INFO("Deleting Song: {}", songPath);

                RuntimeSongLoader::API::DeleteSong(songPath, [&]{
                    if (needSongRefresh) RuntimeSongLoader::API::RefreshSongs(false);
                });
                // if (lobbyGameStateController) lobbyGameStateController->menuRpcManager->SetIsEntitledToLevel(level.value()->get_levelID(), EntitlementsStatus::NotDownloaded);
            } else {
                DEBUG("Level to Delete not found");
            }
            needSongRefresh = true;
            downloadedSongs.erase(downloadedSongs.begin() + selectedIdx);
        }
        catch (il2cpp_utils::RunMethodException const& e) {
            ERROR("REPORT TO ENDER: Exception encountered trying to delete song: {}", e.what());
            e.log_backtrace();
        }
        catch (const std::exception& e) {
            ERROR("REPORT TO ENDER: Exception encountered trying to delete song: {}", e.what());
        }
        list->tableView->ClearSelection();
        list->data->RemoveAt(selectedIdx);
        Refresh();
        modal->Hide();
        cellIsSelected = false;
    }

#pragma endregion // UI events

    ListWrapper<MpDownloadedSongCellInfo*> MpDownloadedSongsGSM::get_data() { return _data; }

#pragma region DownloadedCellInfo
    MpDownloadedSongCellInfo* MpDownloadedSongCellInfo::construct(StringW text, std::string levelId, StringW subText, UnityEngine::Sprite* icon) {
        auto cell = MpDownloadedSongCellInfo::New_ctor();
        cell->text = text;
        cell->levelId = levelId;
        cell->subText = subText;
        cell->icon = icon;
        return cell;
    }

#pragma endregion // DownloadedCellInfo

}
