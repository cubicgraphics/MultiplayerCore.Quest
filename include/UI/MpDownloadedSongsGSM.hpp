#pragma once
#include "custom-types/shared/macros.hpp"
#include "UnityEngine/MonoBehaviour.hpp"
// #include "questui/shared/CustomTypes/Components/List/CustomListTableData.hpp"
#include "bsml/shared/BSML/Components/CustomListTableData.hpp"
#include "bsml/shared/BSML/Components/ModalView.hpp"
#include "HMUI/CurvedTextMeshPro.hpp"
#include "HMUI/ImageView.hpp"
// #include "HMUI/ModalView.hpp"

#include "GlobalNamespace/CustomPreviewBeatmapLevel.hpp"
#include "System/Threading/Tasks/Task_1.hpp"
#include "System/Threading/Tasks/Task.hpp"
#include "System/Action_1.hpp"
#include "UnityEngine/Sprite.hpp"

DECLARE_CLASS_CUSTOM(MultiplayerCore::UI, MpDownloadedSongCellInfo, BSML::CustomCellInfo,
    DECLARE_DEFAULT_CTOR();
    public:
        static MpDownloadedSongCellInfo* construct(StringW text, std::string levelId, StringW subText, UnityEngine::Sprite* icon);
        std::string levelId;
)

DECLARE_CLASS_CODEGEN(MultiplayerCore::UI, MpDownloadedSongsGSM, UnityEngine::MonoBehaviour,
    DECLARE_INSTANCE_FIELD(BSML::ModalView*, modal);
    DECLARE_INSTANCE_FIELD(BSML::CustomListTableData*, list);
    DECLARE_INSTANCE_FIELD(GlobalNamespace::CustomPreviewBeatmapLevel*, lastDownloaded);
    DECLARE_INSTANCE_FIELD(::System::Threading::Tasks::Task*, continueTask);
    DECLARE_INSTANCE_FIELD_PRIVATE_DEFAULT(ListWrapper<MpDownloadedSongCellInfo*>, _data, List<MpDownloadedSongCellInfo*>::New_ctor());

    DECLARE_INSTANCE_METHOD(void, DidActivate, bool firstActivation);
    DECLARE_INSTANCE_METHOD(void, OnEnable);
    DECLARE_INSTANCE_METHOD(void, Update);

    DECLARE_INSTANCE_METHOD(void, SelectCell, HMUI::TableView* tableView, int idx);
    DECLARE_INSTANCE_METHOD(void, Delete);
    DECLARE_INSTANCE_METHOD(void, Keep);

    DECLARE_INSTANCE_METHOD(ListWrapper<MpDownloadedSongCellInfo*>, get_data);

    DECLARE_CTOR(ctor);

    private:
        static MpDownloadedSongsGSM* _instance;

        int selectedIdx;
        bool needSongRefresh;
        bool needRefresh;

        static std::vector<std::string> mapQueue;
        static std::vector<std::string> downloadedSongs;
        static void InsertCell(std::string hash);

        std::string LevelIdAtIndex(int idx);
        void AddCell(UnityEngine::Sprite* coverImageSprite, GlobalNamespace::CustomPreviewBeatmapLevel* level);
        void Refresh();
    public:
        static MpDownloadedSongsGSM* get_instance() { return _instance; }
        static void AddToQueue(std::string hash);
)
