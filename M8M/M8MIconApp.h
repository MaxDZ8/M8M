/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include "M8MIcon.h"
#include "IconCompositer.h"
#include "../Common/NotifyIcon.h"
#include "../Common/LaunchBrowser.h"

/*! The most important part of M8M is... surprisingly, managing the notification icon.
WUT? Yes, that's the most basic for of interface and the only "proper" way to close the application.
It also got a complication already: some people want to control the miner by other means (monitoring processes)
and don't need this at all. */
class M8MIconApp {
public:
    void InitIcon(bool invisible) {
        if(invisible) return;
        compositer.reset(new IconCompositer(M8M_ICON_SIZE, M8M_ICON_SIZE));
        icon.reset(new NotifyIcon);
        icon->SetCaption(M8M_ICON_CAPTION);
        const aubyte white[4] =  { 255, 255, 255, 255 };
	    const aubyte green[4] =  {   0, 255,   0, 255 };
	    const aubyte yellow[4] = {   0, 255, 255, 255 };
	    const aubyte blue[4] =   { 255,   0,   0, 255 };
	    const aubyte red[4] =    {   0,   0, 255, 255 };
		compositer->AddIcon(STATE_ICON_NORMAL, M8M_ICON_16X16_NORMAL);
		compositer->AddIcon(STATE_ICON_LISTENING, M8M_ICON_16X16_LISTENING);
		compositer->AddIcon(STATE_ICON_CLIENT_CONNECTED, M8M_ICON_16X16_CLIENT_CONNECTED);
		compositer->AddState(STATE_OK, white);
		compositer->AddState(STATE_INIT, green);
		compositer->AddState(STATE_WARN, yellow);
		compositer->AddState(STATE_ERROR, red);
		compositer->AddState(STATE_COOLDOWN, blue);
		compositer->SetIconHotspot(8, 7, 14 - 8, 13 - 7); // I just looked at the rasters. Don't ask!

        icon->AddMenuItem(L"Open app folder", []() { OpenFileExplorer(L""); });
        icon->AddMenuSeparator();
        AddMenuItems(*icon);
        icon->AddMenuSeparator();
        icon->AddMenuSeparator();
        icon->AddMenuItem(L"Exit ASAP", [this]() { run = false; });
        ChangeIcon(STATE_ICON_NORMAL);
        ChangeState(STATE_INIT, true);
        icon->BuildMenu();
        icon->ShowMessage(L"Getting ready!\nLeave me some seconds to warm up...");
        icon->Tick();
    }

    void Error(const wchar_t *err, bool wine = true) { // in most cases, it is better to keep the older error state
        if(wine && goneWrong) return;
        Popup(err);
        ChangeState(STATE_ERROR, true);
        goneWrong = true;
    }
    void Error(const std::wstring &err) { Error(err.c_str()); }

    void Warning(const wchar_t *err) {
        Popup(err);
        ChangeState(STATE_WARN, true);
        goneWrong = false; // the state is now warning so pop up back to error or need.
        // Technically a WARN "overwriting" ERROR is wrong but in this specific case it is a good idea to have changing GUIs,
        // they have more chances of capturing user attention.
    }
    void Warning(const std::wstring &err) { Warning(err.c_str()); }

    virtual bool KeepRunning() const { return run; }

protected:
    void Tick() {
        icon->Tick();
    }

    virtual void AddMenuItems(AbstractNotifyIcon &icon) { }

    void ChangeIcon(const char *use, bool update = false) {
        if(icon) {
            compositer->SetCurrentIcon(use);
            if(update) RegenIconBitmap();
        }
    }

    void ChangeState(const char *use, bool update = false) {
        if(icon) {
            compositer->SetCurrentState(use);
            if(update) RegenIconBitmap();
        }
    }

    std::string GetIconName() const {
        return compositer? compositer->GetCurrentIconName() : "";
    }

    std::string GetIconState() const {
        return compositer? compositer->GetCurrentStateName() : "";
    }

    void Popup(const wchar_t *msg) {
        if(icon) icon->ShowMessage(msg);
    }

    void ChangeMenuItem(asizei entry, const wchar_t *msg, std::function<void()> onClick, bool enabled) {
        icon->ChangeMenuItem(entry, msg, onClick, enabled);
    }

private:
    bool run = true;
    bool goneWrong = false;
    std::unique_ptr<AbstractIconCompositer> compositer;
    std::unique_ptr<AbstractNotifyIcon> icon;

    void RegenIconBitmap() {
        std::vector<aubyte> bitmap;
        compositer->GetCompositedIcon(bitmap);
        icon->SetIcon(bitmap.data(), M8M_ICON_SIZE, M8M_ICON_SIZE);
    }
};
