/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
/* M8M miner "main" file.
A bit of history: since this is a miner application, one would assume the big deal should be the mining itself.
Surprise, it's not. It might be for a legacy miner, but M8M is not a legacy miner. It has quite some extensive facilities
such as web interfaces and a mini-gui.
Now, legacy miners being in "everything is a global" mindset take it easy here while M8M pays the price of being structured.
The result is that there are a lot of strings to tie together here and even if someone noticed the main was "only" ~390 lines,
the amount of state was scary: just too many variables and entities involved in pulling up the application.
So, for the first time ever, the main file has received some serious attention and has been restructured by modeling the application
as an hierarchy of classes. This will hopefully also reduce the sheer amount of parameters passed in some calls! */
#include "StartParams.h"
#include "../Common/AREN/SharedUtils/AutoConsole.h"
#include "../Common/AREN/SharedUtils/OSUniqueChecker.h"
#include "M8MWebServingApp.h"
#include <codecvt>


struct StartParamsInferredStructs : StartParams {
    StartParamsInferredStructs(const wchar_t *cmdl) : StartParams(cmdl) { }
    std::wstring configFile;
    std::string algo;
    bool configSpecified = false;

    //! That's really unused but just convenient to keep it there.
    std::unique_ptr<sharedUtils::system::AutoConsole<false>> handyOutputForDebugging;

    //! This controls creation of notification icon and its compositer. It could be handled just like the console but
    //! since this is more complicated, it isn't.
    bool invisible = false;
};


void Parse(std::unique_ptr<OSUniqueChecker> &onlyOne, StartParamsInferredStructs &result) {
    std::vector<wchar_t> value;
    if(result.ConsumeParam(value, L"alreadyRunning")) {
        if(value.size()) {
            const wchar_t *title = L"--alreadyRunning";
            const wchar_t *msg = L"Looks like you've specified a value for a parameter which is not supposed to take any."
                                    L"\nExecution will continue but clean up your command line please.";
            MessageBox(NULL, msg, title, MB_OK | MB_ICONWARNING);
        }
    }
    else {
	    /* M8M is super spiffy and so minimalistic I often forgot it's already running.
	    Running multiple instances might make sense in the future (for example to mine different algos on different cards)
	    but it's not supported for the time being. Having multiple M8M instances doing the same thing will only cause driver work and GPU I$ to work extra hard. */
        onlyOne.reset(new OSUniqueChecker);
	    if(onlyOne->CanStart(L"M8M_unique_instance_systemwide_mutex") == false) {
		    const wchar_t *msg = L"It seems you forgot M8M is already running. Check out your notification area!\n"
				                    L"Running multiple instances is not a good idea in general and it's not currently supported.\n"
								    L"This program will now close.";
		    const wchar_t *title = L"Already running!";
        #if defined(_WIN32)
		    MessageBox(NULL, msg, title, MB_ICONINFORMATION | MB_SYSTEMMODAL | MB_SETFOREGROUND);
        #else
        #error Whoops! Tell the user to not do that!
        #endif
		    throw (void*)nullptr;
        }
    }

    if(result.ConsumeParam(value, L"console")) {
        if(_wcsicmp(L"new", value.data()) == 0) result.handyOutputForDebugging = std::make_unique<sharedUtils::system::AutoConsole<false>>();
#if defined(_WIN32)
        else if(_wcsicmp(L"parent", value.data()) == 0) result.handyOutputForDebugging = std::make_unique<sharedUtils::system::AutoConsole<false>>(auint(~0));
#endif
        else {
            auto number(_wtoll(value.data()));
            if(number < 0 || number > auint(~0)) throw std::exception("Invalid value for parameter --console");
            result.handyOutputForDebugging = std::make_unique<sharedUtils::system::AutoConsole<false>>(auint(number));
        }
	    result.handyOutputForDebugging->Enable();
    }
    if(result.ConsumeParam(value, L"config")) {
        result.configFile = value.data();
        result.configSpecified = true;
    }
    else result.configFile = L"init.json";

    if(result.ConsumeParam(value, L"invisible")) {
        if(value.size()) {
            const wchar_t *title = L"--invisible";
            const wchar_t *msg = L"Looks like you've specified a value for a parameter which is not supposed to take any."
                                    L"\nExecution will continue but clean up your command line please.";
            MessageBox(NULL, msg, title, MB_OK | MB_ICONWARNING);
        }
        result.invisible = true;
    }

    if(result.ConsumeParam(value, L"algo")) {
        if(value.empty()) {
            const wchar_t *title = L"--algo";
            const wchar_t *msg = L"This parameter requires a value to be specified. Parameter ignored."
                                    L"\nExecution will continue but clean up your command line please.";
            MessageBox(NULL, msg, title, MB_OK | MB_ICONWARNING);
        }
        else {
            result.algo.reserve(value.size());
            for(auto c : value) result.algo.push_back(char(c));
        }
    }

    if(result.FullyConsumed() == false) {
        const wchar_t *title = L"Dirty command line";
        const std::wstring rem(result.GetRemLine());
        const std::wstring msg = L"Command line parameters couldn't consume everything you wrote."
                                L"\nExecution will continue but clean up your command line please."
                                L"\nUnused command line parts:\n"
                                +
                                rem;
        MessageBox(NULL, msg.c_str(), title, MB_OK | MB_ICONWARNING);
    }
}


#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE unusedLegacyW16, PWSTR cmdLine, int showStatus) {
#else
int main(int argc, char **argv) {
#endif
    auto fatal = [](const wchar_t *msg) {
        MessageBox(NULL, msg, L"Fatal error!", MB_ICONERROR | MB_SYSTEMMODAL | MB_SETFOREGROUND);
    };
    auto fatalAscii = [&fatal](const char *msg) {
		std::string utfbyte(msg);
		std::wstring_convert< std::codecvt_utf8_utf16<wchar_t> > convert;
		auto unicode(convert.from_bytes(utfbyte));
        fatal(unicode.c_str());
    };

    try {
        StartParamsInferredStructs start(cmdLine);
        std::unique_ptr<OSUniqueChecker> sysSemaphore;
        Parse(sysSemaphore, start);
        bool run = true, reboot = false;
        while(run) {
            if(reboot) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                reboot = false;
            }
            Network networkWrapper;
            M8MWebServingApp application(networkWrapper);
            application.LoadKernelDescriptions(L"algorithms.json", "kernels/");
            application.InitIcon(start.invisible);
            std::unique_ptr<Settings> config(application.LoadSettings(start.configFile, start.configSpecified, start.algo.size()? start.algo.c_str() : nullptr));
            if(config) { // pool setup
                application.SetReconnectDelay(config->reconnDelay);
                for(asizei init = 0; init < config->pools.size(); init++) {
                    if(application.AddPool(*config->pools[init]) == false) {
                        application.Error(L"Unknown pool[" + std::to_wstring(init) + L"] algorithm");
                        config.reset();
                        break;
                    }
                }
                if(config) application.BeginPoolActivation(config->algo.c_str());
            }
            application.EnumerateDevices();
            if(config) { // pulling up the miner.
                application.StartMining(config->algo, config->implParams);
            }
            config.reset();
            while(run = application.KeepRunning()) {
                std::vector<Network::SocketInterface*> toRead, toWrite;
                application.FillSleepLists(toRead, toWrite);
                const std::chrono::milliseconds tickTime(200);
                if(toRead.size() || toWrite.size()) networkWrapper.SleepOn(toRead, toWrite, tickTime.count());
                else std::this_thread::sleep_for(tickTime);
                application.Refresh(toRead, toWrite);
            }
            reboot = application.Reboot();
            if(reboot) run = true;
        }
    }
    catch(const char *msg) { fatalAscii(msg); }
    catch(const wchar_t *msg) { fatal(msg); }
    catch(const void *) { } // silent exception... comes handy when the specific path has more information to pull out than a single string
    catch(const std::wstring msg) { fatal(msg.c_str()); }
    catch(const std::string msg) { fatalAscii(msg.c_str()); }
    catch(const std::exception msg) { fatalAscii(msg.what()); }
    catch(...) { fatalAscii("An unknown error was detected.\nThis is very wrong, program will now exit."); }
    return 0;
}
