/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once


//! Parameters passed from command line.
struct StartParams {
    StartParams(const wchar_t *params) { 
        const asizei takes = wcslen(params) + 1;
        parameters.reset(new wchar_t[takes]);
        wcscpy_s(parameters.get(), takes, params);
    }
    
    bool ConsumeParam(std::vector<wchar_t> &value, const wchar_t *name) {
	    bool found = false;
        asizei begin;
        asizei loop;
        wchar_t *cmdLine = parameters.get();
	    for(loop = 0; cmdLine[loop]; loop++) {
            begin = loop;
            if(cmdLine[loop] <= 32) { }
            else if(wcsncmp(cmdLine + loop, L"--", 2) == 0) {
                loop += 2;
                if(_wcsnicmp(cmdLine + loop, name, wcslen(name)) == 0) {
                    loop += wcslen(name);
                    if(cmdLine[loop] <= 32 || cmdLine[loop] == '=') {
                        found = true;
                        break;
                    }
                }
                else { // not the parameter I'm looking for. Go to a param init following a blank.
                    bool blank = false;
                    while(cmdLine[loop]) {
                        if(cmdLine[loop] <= 32) blank = true;
                        else if(blank) {
                            blank = false;
                            if(wcsncmp(cmdLine + loop, L"--", 2) == 0) {
                                loop--;
                                break;
                            }
                        }
                        loop++;
                    }
                }
            }
            else { // parameters always follow a blank
                while(cmdLine[loop] && cmdLine[loop] > 32) loop++; // there are really more blanks in unicode but I don't care.
                loop--;
            }
        }
        if(!found) return false;
        bool blank = false;
        if(cmdLine[loop] == '=') loop++;
        else if(cmdLine[loop] <= 32 && cmdLine[loop]) while(cmdLine[loop] && cmdLine[loop] <= 32) { loop++;    blank = true; }
        // Eat everything till next param.
        const asizei start(loop);
        while(cmdLine[loop]) {
            if(cmdLine[loop] <= 32) blank = true;
            else if(blank) {
                blank = false;
                if(wcsncmp(cmdLine + loop, L"--", 2) == 0) break;
            }
            loop++;
        }
        if(loop != start) {
            value.resize(loop - start + 1);
            for(asizei cp = start; cp < loop; cp++) value[cp - start] = cmdLine[cp];
            value[loop - start] = 0;
            for(asizei clear = loop - start; clear; clear--) {
                if(value[clear] <= 32) value[clear] = 0;
                else break;
            }
        }
        
        // Consume chars so we can detect if parameters were not completely mangled.
        asizei dst = begin;
        for(asizei src = loop; cmdLine[src]; src++, dst++) cmdLine[dst] = cmdLine[src];
        cmdLine[dst] = 0;
        while(dst && cmdLine[dst] <= 32) cmdLine[dst--] = 0;
        return true;
    }
    bool FullyConsumed() const { return parameters.get()? wcslen(parameters.get()) == 0 : true; }

private:
    std::unique_ptr<wchar_t[]> parameters;
};