#include "assets.hpp"
#include "logging.hpp"
#include "UI/MpDownloadedSongsGSM.hpp"
//#include "Utilities.hpp"

// #include "questui/shared/BeatSaberUI.hpp"
#include "bsml/shared/BSML.hpp"
// #include "bsml/shared/Helpers/creation.hpp"
//#include "questui/shared/CustomTypes/Components/MainThreadScheduler.hpp"
#include "lapiz/shared/utilities/MainThreadScheduler.hpp"
#include "songloader/shared/API.hpp"

#include "GlobalNamespace/IMenuRpcManager.hpp"

#include "HMUI/ModalView.hpp"

#include "UnityEngine/UI/ContentSizeFitter.hpp"
#include "UnityEngine/UI/LayoutElement.hpp"

//#include "GlobalFields.hpp"
//#include "shared/GlobalFields.hpp"
using namespace GlobalNamespace;
//using namespace QuestUI;
using namespace BSML;
using namespace RuntimeSongLoader::API;
using namespace HMUI;
using namespace MultiplayerCore;


DEFINE_TYPE(MultiplayerCore::UI, MpDownloadedSongsGSM);

namespace MultiplayerCore::UI {
    bool cellIsSelected = false;
    MpDownloadedSongsGSM* MpDownloadedSongsGSM::_instance;
    std::vector<std::string> MpDownloadedSongsGSM::mapQueue;
    std::vector<std::string> MpDownloadedSongsGSM::downloadedSongs;

    void MpDownloadedSongsGSM::CreateCell(System::Threading::Tasks::Task_1<UnityEngine::Sprite*>* coverTask, CustomPreviewBeatmapLevel* level) {
        DEBUG("CreateCell");
        try {
        UnityEngine::Sprite* cover = coverTask->get_Result();
        // If we have a coverImage and a level we add them to the list
        if (cover && level) {
            // "<size=80%><noparse>" + map.GetMetadata().GetSongAuthorName() + "</noparse>" + " <size=90%>[<color=#67c16f><noparse>" + map.GetMetadata().GetLevelAuthorName() + "</noparse></color>]"
            list->data->Add(BSML::CustomCellInfo::construct(
                level->get_songName() ? level->get_songName() : "Error: songName null",
                (level->get_songAuthorName() ? static_cast<std::string>(level->get_songAuthorName()) : std::string()) + " [" + (level->get_levelAuthorName() ? static_cast<std::string>(level->get_levelAuthorName()) : std::string()) + "]",
                cover
            ));
        }
        // else if we have the level but no coverImage we add the level and use the default coverImage
        else if (level) {
            list->data->Add(BSML::CustomCellInfo::construct(
                level->get_songName() ? level->get_songName() : "Error: songName null",
                (level->get_songAuthorName() ? static_cast<std::string>(level->get_songAuthorName()) : std::string()) + " [" + (level->get_levelAuthorName() ? static_cast<std::string>(level->get_levelAuthorName()) : std::string()) + "]",
                level->get_defaultCoverImage()
            ));
        } else ERROR("Nullptr: cover '{}', level '{}'", fmt::ptr(cover), fmt::ptr(level));
        if (!mapQueue.empty()) {
            InsertCell(mapQueue.back());
            mapQueue.pop_back();
        }
        downloadedSongs.emplace_back(level->get_levelID());
        // Refresh our view, though this doesn't always work perfectly first try though will at least show the next time our view is re-enabled
        // if (list && list->tableView)
            // list->tableView->RefreshCellsContent();
        // else ERROR("Nullptr: list '{}', list->tableView '{}'", fmt::ptr(list), fmt::ptr(list->tableView));
        Refresh();
        }
        catch (il2cpp_utils::RunMethodException const& e) {
            ERROR("REPORT TO ENDER: Exception encountered trying to add song to list: {}", e.what());
            e.log_backtrace();
        }
        catch (const std::exception& e) {
            ERROR("REPORT TO ENDER: Exception encountered trying to add song to list: {}", e.what());
        }
    }

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
        list->data->RemoveAt(selectedIdx);
        Refresh();
        modal->Hide();
        cellIsSelected = false;
    }

    // TODO: Add index check, check if index is out of bounds
    void MpDownloadedSongsGSM::Delete() {
        try {
            needSongRefresh = false;
            auto level = GetLevelById(downloadedSongs.at(selectedIdx));
            if (level.has_value()) {
                std::string songPath = to_utf8(csstrtostr(level.value()->get_customLevelPath()));
                getLogger().info("Deleting Song: %s", songPath.c_str());
                DeleteSong(songPath, [&] {
                    if (needSongRefresh) {
                        RefreshSongs(false);
                    }
                    });
                // if (lobbyGameStateController) lobbyGameStateController->menuRpcManager->SetIsEntitledToLevel(level.value()->get_levelID(), EntitlementsStatus::NotDownloaded);
            }
            else {
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

    // TODO: Add keep all and delete all option
    void MpDownloadedSongsGSM::DidActivate(bool firstActivation) {
        DEBUG("MpDownloadedSongsGSM::DidActivate");
        if (firstActivation) {
            _instance = this;

            BSML::parse_and_construct(IncludedAssets::DownloadedSongs_bsml, get_transform(), this);
            // modal = BeatSaberUI::CreateModal(get_transform(), { 55, 25 }, [this](HMUI::ModalView* self) {
            //     list->tableView->ClearSelection();
            //     });
            // auto wrapper = QuestUI::BeatSaberUI::CreateHorizontalLayoutGroup(modal->get_transform());
            // auto container = QuestUI::BeatSaberUI::CreateVerticalLayoutGroup(wrapper->get_transform());
            // container->set_childAlignment(UnityEngine::TextAnchor::MiddleCenter);
            // BSML::Helpers::CreateText(container->get_transform(), "Do you want to delete this song?")->set_alignment(TMPro::TextAlignmentOptions::Center);

            // auto horizon = QuestUI::BeatSaberUI::CreateHorizontalLayoutGroup(container->get_transform());

            // BSML::Helpers::CreateUIButton(horizon->get_transform(), "<color=#ff0000>Delete</color>", [this]() -> void {
            //     Delete();
            //     cellIsSelected = false;
            //     });

            // BSML::Helpers::CreateUIButton(horizon->get_transform(), "<color=#00ff00>Keep</color>", [this]() -> void {
            //     DownloadedSongIds.erase(DownloadedSongIds.begin() + selectedIdx);
            //     list->tableView->ClearSelection();
            //     list->data->RemoveAt(list->data.begin() + selectedIdx);
            //     Refresh();
            //     modal->Hide(true, nullptr);
            //     cellIsSelected = false;
            // });

            // auto vertical = QuestUI::BeatSaberUI::CreateVerticalLayoutGroup(get_transform());
            // vertical->get_gameObject()->AddComponent<UnityEngine::UI::ContentSizeFitter*>()
            //     ->set_verticalFit(UnityEngine::UI::ContentSizeFitter::FitMode::MinSize);


            // list = BeatSaberUI::CreateScrollableList(vertical->get_transform(), { 80, 53 }, [this](int idx) {
            //     DEBUG("Cell with idx {} clicked", idx);
            //     cellIsSelected = true;
            //     selectedIdx = idx;
            //     modal->Show(true, true, nullptr);
            // });

            // auto autoDelete = QuestUI::BeatSaberUI::CreateToggle(vertical->get_transform(), "Auto-Delete Songs", getConfig().config["autoDelete"].GetBool(), [](bool value) {
            //     getConfig().config["autoDelete"].SetBool(value);
            //     getConfig().Write();
            // });
            // BSML::Helpers::AddHoverHint(autoDelete->get_gameObject(), "Automatically deletes downloaded songs after playing them.");
        }

        // InsertCell("5E3AFB8269F6D06EE9D1839EBF413B947DC931FF");

        DEBUG("DownloadedSongsGSM::DidActivate");
        Refresh();
    }

    void MpDownloadedSongsGSM::InsertCell(std::string hash) {
        DEBUG("DownloadedSongsGSM::InsertCell");
        std::optional<CustomPreviewBeatmapLevel*> levelOpt = GetLevelByHash(hash);
        MpDownloadedSongsGSM* instance = get_Instance();
        if (levelOpt.has_value() && instance) {
            CustomPreviewBeatmapLevel* level = levelOpt.value();
            instance->lastDownloaded = level;
            INFO("Song with levelId '{}' added to list", level->get_levelID());
            System::Threading::Tasks::Task_1<UnityEngine::Sprite*>* coverTask = instance->lastDownloaded->GetCoverImageAsync(System::Threading::CancellationToken::get_None());
            System::Action_1<System::Threading::Tasks::Task*>* action = custom_types::MakeDelegate<System::Action_1<System::Threading::Tasks::Task*>*>(
                (std::function<void(System::Threading::Tasks::Task_1<UnityEngine::Sprite*>*)>)[](System::Threading::Tasks::Task_1<UnityEngine::Sprite*>* coverTask) {
                    // Note: coverTask should actually be of type Task*, but we know we only call ContinueWith on an instance where our overall task is of this type, so we can skip the cast and do it implicitly in the parameter
                    DEBUG("DownloadedSongsGSM::InsertCell Call CreateCell");
                    get_Instance()->CreateCell(coverTask, get_Instance()->lastDownloaded);
                }
            );
            instance->continueTask = reinterpret_cast<System::Threading::Tasks::Task*>(coverTask)->ContinueWith(action);

        }
        else if (levelOpt.has_value())
        {
            DEBUG("DownloadedSongsGSM::InsertCell: Instance is nullptr, putting hash '{}' in queue", hash);
            mapQueue.push_back(hash);
        }
        else {
            ERROR("Song with hash '{}' not found, was it already deleted?", hash);
        }
    }

    void MpDownloadedSongsGSM::AddToQueue(std::string hash) {
        mapQueue.push_back(hash);
    }

    // void MpDownloadedSongsGSM::InsertCell(GlobalNamespace::CustomPreviewBeatmapLevel* level) {
    //     DEBUG("DownloadedSongsGSM::InsertCell");
    //     MpDownloadedSongsGSM* instance = get_Instance();
    //     if (level && instance) {
    //         instance->lastDownloaded = level;
    //         INFO("Song with levelId '{}' added to list", level->get_levelID());
    //         System::Threading::Tasks::Task_1<UnityEngine::Sprite*>* coverTask = instance->lastDownloaded->GetCoverImageAsync(System::Threading::CancellationToken::get_None());
    //         System::Action_1<System::Threading::Tasks::Task*>* action = custom_types::MakeDelegate<System::Action_1<System::Threading::Tasks::Task*>*>(
    //             (std::function<void(System::Threading::Tasks::Task_1<UnityEngine::Sprite*>*)>)[](System::Threading::Tasks::Task_1<UnityEngine::Sprite*>* coverTask) {
    //                 // Note: coverTask should actually be of type Task*, but we know we only call ContinueWith on an instance where our overall task is of this type, so we can skip the cast and do it implicitly in the parameter
    //                 DEBUG("DownloadedSongsGSM::InsertCell Call CreateCell");
    //                 get_Instance()->CreateCell(coverTask, get_Instance()->lastDownloaded);
    //             }
    //         );
    //         instance->continueTask = reinterpret_cast<System::Threading::Tasks::Task*>(coverTask)->ContinueWith(action);
    //     }
    //     // else if (level) {
    //     //     DEBUG("DownloadedSongsGSM::InsertCell: Instance is nullptr, putting levelId '{}' in queue", level->get_levelID());
    //     //     mapQueue.push_back(Utilities::HashForLevelId(level->get_levelID());
    //     // }
    //     else {
    //         ERROR("Level is nullptr, was it already deleted?");
    //     }
    // }

    void MpDownloadedSongsGSM::Refresh() {
        DEBUG("DownloadedSongsGSM::Refresh");
        list->tableView->ReloadData();
        list->tableView->RefreshCellsContent();
    }

    void MpDownloadedSongsGSM::OnEnable() {
        if (list && list->tableView) {
            if (!mapQueue.empty()) {
                InsertCell(mapQueue.back());
                mapQueue.pop_back();
            }
            if (cellIsSelected) list->tableView->ClearSelection();
            list->tableView->ReloadData();
            list->tableView->RefreshCellsContent();
        }
    }
}