/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#include "M8MWebServingApp.h"



void M8MWebServingApp::Refresh(std::vector<Network::SocketInterface*> &toRead, std::vector<Network::SocketInterface*> &toWrite) {
    // Rather than driving the icon on events in callbacks, look at server states and figure out what to do each time.
    // It is way less efficient but this is not a performance path and this is much cleaner.
    M8MMinerTrackingApp::Refresh(toRead, toWrite);
    if(monitor.server) monitor.server->Refresh(toRead, toWrite);
    if(admin.server) admin.server->Refresh(toRead, toWrite);
    asizei listening = 0;
    auto accepting = [](const std::unique_ptr<AbstractWSServer> &server) { return server && server->AreYouListening() && server->AreYouClosing() == false; };
    if(accepting(monitor.server)) listening++;
    if(accepting(admin.server)) listening++;
    const char *icon = STATE_ICON_NORMAL;
    if(listening) {
        asizei connected = 0;
        if(monitor.server) connected += monitor.server->GetNumClients();
        if(admin.server) connected += admin.server->GetNumClients();
        if(connected) icon = STATE_ICON_CLIENT_CONNECTED;
        else icon = STATE_ICON_LISTENING;
    }
    if(GetIconName() != icon) ChangeIcon(icon, true);
    // Not quite accurate but who cares, it comes easy
    using namespace std::chrono;
    if(startTime.firstNonce == std::chrono::system_clock::time_point()) {
        bool found = false;
        const auto zero = std::chrono::system_clock::time_point();
        for(auto &test : deviceShares) {
            if(test.first != zero) { // the assumption is that two devices are unlikely to produce a nonce in the same tick and that's not important anyway
                startTime.firstNonce = test.first;
                break;
            }
        }
    }
}


void M8MWebServingApp::AddMenuItems(AbstractNotifyIcon &icon) {
    auto onAdminEnableClick = [this, &icon]() {
        if(!admin.server) {
            auto put(std::make_unique<AbstractWSServer>(network, 31001, "admin", "M8M-admin"));
            put->clientConnectionCallback = [this](aint change, asizei count) {
                WebConnectionEvent(change, count, "admin");
            };
            put->serviceStateCallback = [this](bool starting) {
                if(starting == false) admin.server.reset();
            };
            RegisterMonitorCommands(*put);
            RegisterAdminCommands(*put);
            icon.ChangeMenuItem(admin.openConnMI, L"Connect to web admin", []() { LaunchWebApp(L"admin_localhost.html"); });
            icon.SetMenuItemStatus(admin.closeMI, true);
            admin.server = std::move(put);
            admin.server->Listen();
        }
        LaunchWebApp(L"admin_localhost.html");
    };
    admin.openConnMI = icon.AddMenuItem(GetInitialMenuMessage("admin"), onAdminEnableClick);
    admin.closeMI = icon.AddMenuItem(L"Close web admin", [this, &icon, onAdminEnableClick]() {
        icon.SetMenuItemStatus(admin.closeMI, false);
        icon.ChangeMenuItem(admin.openConnMI, GetInitialMenuMessage("admin"), onAdminEnableClick);
        admin.server->BeginClose();
    }, false);

    icon.AddMenuSeparator();

    auto onMonitorEnableClick = [this, &icon]() {
        if(!monitor.server) {
            auto put(std::make_unique<AbstractWSServer>(network, 31000, "monitor", "M8M-monitor"));
            put->clientConnectionCallback = [this](aint change, asizei count) {
                WebConnectionEvent(change, count, "monitor");
            };
            put->serviceStateCallback = [this](bool starting) {
                if(starting == false) monitor.server.reset();
            };
            RegisterMonitorCommands(*put);
            icon.ChangeMenuItem(monitor.openConnMI, L"Connect to web monitor", []() { LaunchWebApp(L"monitor_localhost.html"); });
            icon.SetMenuItemStatus(monitor.closeMI, true);
            monitor.server = std::move(put);
            monitor.server->Listen();
        }
        LaunchWebApp(L"monitor_localhost.html");
    };
    monitor.openConnMI = icon.AddMenuItem(GetInitialMenuMessage("monitor"), onMonitorEnableClick);
    monitor.closeMI = icon.AddMenuItem(L"Close web monitor", [this, &icon, onMonitorEnableClick]() { // close functions are similar to each other
        icon.SetMenuItemStatus(monitor.closeMI, false);
        icon.ChangeMenuItem(monitor.openConnMI, GetInitialMenuMessage("monitor"), onMonitorEnableClick);
        monitor.server->BeginClose();
        ChangeIcon(STATE_ICON_NORMAL, true);
    }, false);
}


void M8MWebServingApp::RegisterMonitorCommands(AbstractWSServer &server) {
    using namespace commands::monitor;
    RegisterCommand(server, new SystemInfoCMD(*this));
    RegisterCommand(server, new AlgosCMD(sources));
    RegisterCommand(server, new PoolCMD(*this));
    RegisterCommand(server, new RejectReasonCMD(*this));
    RegisterCommand(server, new ConfigInfoCMD(*this));
    RegisterCommand(server, new ScanTime(perfStats));
    RegisterCommand(server, new DeviceShares(*this));
    RegisterCommand(server, new PoolStats(*this));
    RegisterCommand(server, new UptimeCMD(*this));
    RegisterCommand(server, new commands::VersionCMD);
    RegisterCommand(server, new commands::ExtensionListCMD(extensions));
    RegisterCommand(server, new commands::UpgradeCMD(extensions));
    RegisterCommand(server, new commands::UnsubscribeCMD(server));
}


void M8MWebServingApp::RegisterAdminCommands(AbstractWSServer &server) {
    using namespace commands::admin;
    RegisterCommand(server, new ConfigFileCMD(*this));
    RegisterCommand(server, new GetRawConfigCMD(rawConfig));
    RegisterCommand(server, new SaveRawConfigCMD(configFile.c_str()));
    RegisterCommand(server, new ReloadCMD([this]() {
        reloadRequested = true;
        return false; // will reload listening?
    }));
}
