/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "M8MMinerTrackingApp.h"
#include "AbstractWSServer.h"

#include "commands/Monitor/ConfigInfoCMD.h"
#include "commands/Monitor/UptimeCMD.h"
#include "commands/VersionCMD.h"
#include "commands/ExtensionListCMD.h"
#include "commands/UpgradeCMD.h"
#include "commands/UnsubscribeCMD.h"

#include "commands/Admin/GetRawConfigCMD.h"
#include "commands/Admin/SaveRawConfigCMD.h"
#include "commands/Admin/ReloadCMD.h"
#include "commands/Admin/ConfigFileCMD.h"


class M8MWebServingApp : public M8MMinerTrackingApp,
                         commands::monitor::UptimeCMD::StartTimeProvider,
                         commands::admin::ConfigFileCMD::ConfigInfoProviderInterface {
public:
    M8MWebServingApp(NetworkInterface &network) : M8MMinerTrackingApp(network) { }
    
    commands::admin::RawConfig rawConfig;
    std::wstring configFile;
    bool explicitConf = false, redirectedConf = false;

    struct {
        std::chrono::system_clock::time_point program, hashing, firstNonce;
    } startTime;

    bool reloadRequested = false;

    //! \note Those changes are applied next time the corresponding service is started.
    void SetMonitor(aushort port, const char *resURI, const char *wsProtocol) {
        monitor.init.port = port;
        monitor.init.resURI = resURI;
        monitor.init.wsProtocol = wsProtocol;
    }
    void SetAdmin(aushort port, const char *resURI, const char *wsProtocol) {
        admin.init.port = port;
        admin.init.resURI = resURI;
        admin.init.wsProtocol = wsProtocol;
    }
    bool Reboot() const { return reboot; }
    
    void FillSleepLists(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
        M8MMinerTrackingApp::FillSleepLists(toRead, toWrite);
        if(monitor.server) monitor.server->FillSleepLists(toRead, toWrite);
        if(admin.server) admin.server->FillSleepLists(toRead, toWrite);
	}

    void Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite);

private:
    bool reboot = false;
    std::map<std::string, commands::ExtensionState> extensions; //!< nothing really defined for the time being

    struct Service {
        struct InitDesc {
            aushort port;
            const char *resURI, *wsProtocol;
        } init;
        auint openConnMI, closeMI;
        std::unique_ptr<AbstractWSServer> server;
    } monitor, admin;

    void AddMenuItems(AbstractNotifyIcon &icon);

    
    void WebConnectionEvent(asizei change, asizei count, const std::string &what) {
		if(count == 0) {
            if(change > 0) ChangeIcon(STATE_ICON_CLIENT_CONNECTED);
            else ChangeIcon(STATE_ICON_LISTENING);
        }
    }
    
    void RegisterMonitorCommands(AbstractWSServer &server);
    void RegisterAdminCommands(AbstractWSServer &server);

    void RegisterCommand(AbstractWSServer &server, commands::AbstractCommand *ptr) {
        std::unique_ptr<commands::AbstractCommand> own(ptr);
        server.RegisterCommand(own);
    }


    static void LaunchWebApp(const std::wstring &htmlRelative) {
#if defined (_DEBUG)
        const wchar_t *pathToWebApps = L".." DIR_SEPARATOR L"web" DIR_SEPARATOR;
#else
        const std::wstring pathToWebApps = L"";
#endif
        const std::wstring webAppPath(pathToWebApps + htmlRelative);
        LaunchBrowser(webAppPath.c_str());
    }

    static const wchar_t* GetInitialMenuMessage(const std::string &what) {
        if(what == "monitor") return L"Open and connect web monitor";
        else return L"Open and connect web admin";
    }

    // commands::monitor::UptimeCMD::StartTimeProvider //////////////////////////////////////////////////////
    std::chrono::seconds GetStartTime(commands::monitor::UptimeCMD::StartTime st) const {
        using namespace commands::monitor;
        auto when(startTime.program);
        switch(st) {
        case UptimeCMD::st_hashing: when = startTime.hashing; break;
        case UptimeCMD::st_firstNonce: when = startTime.firstNonce; break;
        }
        return std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch());

    }

    // commands::monitor::UptimeCMD::StartTimeProvider //////////////////////////////////////////////////////
    void Unsubscribe(const std::string &command, const std::string &stream);

    // commands::admin::ConfigFileCMD::ConfigInfoProviderInterface //////////////////////////////////////////
    std::wstring Filename() const { return configFile; }
    bool Explicit() const { return explicitConf; }
    bool Redirected() const { return redirectedConf; }
    bool Valid() const { return rawConfig.good.IsObject(); }
};
