#include "hooking.hpp"
#include "logging.hpp"

#include "custom-types/shared/register.hpp"

#include "Installers/MpAppInstaller.hpp"
#include "Installers/MpMenuInstaller.hpp"

#include "UI/MpDownloadedSongsGSM.hpp"

#include "lapiz/shared/zenject/Zenjector.hpp"
#include "bsml/shared/BSML.hpp"

ModInfo modInfo{MOD_ID, VERSION};

Logger& getLogger() {
    static Logger* logger = new Logger(modInfo, LoggerOptions(false, true));
    return *logger;
}

extern "C" void setup(ModInfo& info) {
    info = modInfo;
}

extern "C" void load() {
    auto& logger = getLogger();

    il2cpp_functions::Init();
    custom_types::Register::AutoRegister();
    Hooks::InstallHooks(logger);
    BSML::Init();

    BSML::Register::RegisterGameplaySetupTab<MultiplayerCore::UI::MpDownloadedSongsGSM*>("MpDownloads", BSML::MenuType::Online);

    auto zenjector = Lapiz::Zenject::Zenjector::Get();
    zenjector->Install<MultiplayerCore::Installers::MpAppInstaller*>(Lapiz::Zenject::Location::App);
    zenjector->Install<MultiplayerCore::Installers::MpMenuInstaller*>(Lapiz::Zenject::Location::Menu);
}
