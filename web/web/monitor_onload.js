"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

// Deals mostly with building the static information that never changes.
// Building the static data and layout is usually quite simple so it's all done here
// so this is bigger than I'd like it to.
window.onload = function() {
    window.keepUntilConnect = document.getElementById("monitoring");
    window.keepUntilConnect.parentNode.removeChild(window.keepUntilConnect);

    var CONNECTION_TIMEOUT = 15;
    var points = 0, ticked = 0;
    var maxPoints = 3;
    function failure(reason) {
        /*! todo: find a way for better error messaging? */
        var initializing = document.getElementById("initializing");
        var monitoring = document.getElementById("monitoring");
        if(initializing) {
            document.getElementById("pre").textContent = "Failed to connect to";
            document.getElementById("advancing").textContent = ", this is a fatal error (" + reason + ")";
            var bar = document.getElementById("waitHint");
            if(bar) bar.parentNode.removeChild(bar);
        }
        else /*if(monitoring) guaranteed to be here */ {
            
        }
    };
    var serverHost = "localhost";
    var serverPort = 31000;
    var callbacks = {
        fail: failure,
        close: function() { alert("Connection closed.\nFrom now on, visualized data will be referring to last received."); },
        success: function() { // because the server replies (in order but) asynchronously we must make sure we build everything before sending next cmd
            minerMonitor.requestSimple('version', version_callback);
        },
        pingTimeFunc: function(time) {
            if(target) document.getElementById("ping").textContent = Math.ceil(time * 1000);
        },
        error: function(string) {
            alert('Error reply!\n\n' + string);
        }
    };
    var target = document.getElementById("hostname");
    target.textContent = "localhost";
    window.minerMonitor = new MinerMonitor(serverHost, serverPort, "monitor", callbacks);
    
    function version_callback(gotVer) {
        var wanted = 4;
        if(gotVer.protocol !== wanted) {
            failure("Version mismatch. Got " + gotVer + " but must be " + wanted);
            window.minerMonitor.socket.close();
            delete window.minerMonitor;
        
            minerMonitor.requestSimple('uptime', uptime_callback);
            return;
        }
        document.body.removeChild(document.getElementById("initializing"));
        document.body.appendChild(window.keepUntilConnect);
        delete window.keepUntilConnect;
        
        document.getElementById("server").textContent = serverHost + ":" + serverPort;
        window.miner.compiled = gotVer.build.date + ' (' + gotVer.build.time + ')';
        window.miner.msg = gotVer.build.msg;
        var container = document.getElementById("serverInfo");
        var div = window.appHelp.makeFloatingDetailsDiv();
        container.parentNode.appendChild(div);
        container.appendChild(window.appHelp.makeDetailShowHideButton(div, "Miner informations"));
        var algoTable = '<table><thead><tr>';
        algoTable += '<th>Algorithm name</th><th>Implementation</th><th>Version</th><th>Signature</th>';
        algoTable += '</tr></thead><tbody id="algoTable"></tbody></table>';
        div.innerHTML = '<h3>Miner informations</h2>Compiled ' + window.miner.compiled + '<br>' +
            (window.miner.msg.length? '<br>' + window.miner.msg : '') + algoTable +
            "<h3>Run time</h2>" + 
            "Since program initialized: <span id='progElapsed'></span><br>" +
            "Since hashing started: <span id='hashElapsed'></span><br>" +
            "First nonce found: <span id='firstNonceTime'></span><br>";
        div.appendChild(window.appHelp.makeDetailShowHideButton(div, "Close"));
        document.getElementById('perfMode').onchange = refreshPerformanceCells;
        
        minerMonitor.requestSimple('uptime', uptime_callback);
    }
    
    function uptime_callback(obj) {
        window.miner.programStart = new Date(obj.program * 1000);
        window.miner.hashingStart = new Date(obj.hashing * 1000);
        var fnt = document.getElementById('firstNonceTime');
        if(obj.nonce) {
            window.miner.firstNonce = new Date(obj.nonce * 1000);
            fnt.textContent = window.miner.firstNonce.toLocaleString();
        }
        else fnt.innerHTML = '<em>Not found yet.</em>';
        window.setInterval(function() {
            var now = new Date();
            document.getElementById('progElapsed').textContent = window.appHelp.readableHHHHMMSS(now - window.miner.programStart);
            document.getElementById('hashElapsed').textContent = window.appHelp.readableHHHHMMSS(now - window.miner.hashingStart);
        }, 1000);
        
        minerMonitor.requestSimple('algos', algos_callback);
    }
    
    function algos_callback(algos) {
        window.miner.algos = algos;
        for(var key in algos) {
            for(var imp = 0; imp < algos[key].length; imp++) {
                var mem = {};
                mem.name = algos[key][imp][0];
                mem.version = algos[key][imp][1];
                mem.signature = algos[key][imp][2];
                algos[key][imp] = mem;
            }
        }
        var algoTable = document.getElementById('algoTable');
        for(var key in algos) {
            for(var imp = 0; imp < algos[key].length; imp++) {
                var inner = "";
                if(imp === 0) inner += '<td rowspan="' + algos[key].length + '">' + key + '</td>';
                inner += '<td>' + algos[key][imp].name + '</td>';
                inner += '<td>' + algos[key][imp].version + '</td>';
                inner += '<td>' + algos[key][imp].signature + '</td>';
                var tr = document.createElement("tr");
                tr.innerHTML = inner;
                algoTable.appendChild(tr);
            }
        }
        
        minerMonitor.requestSimple('systemInfo', systemInfo_callback);
    }
    
    function systemInfo_callback(reply) {
        miner.api = reply.API;
        miner.platforms = reply.platforms;
        
        document.getElementById('M8M_API').innerHTML = miner.api;
        var linearIndex = 0;
        var tbody = document.getElementById('sysInfoDevices');
        var TABLE_COL_COUNT = quirkyColCount(tbody);
        for(var pi in miner.platforms) {
            var platDesc = "<em>" + miner.platforms[pi].name + "</em><br>by " +
                     miner.platforms[pi].vendor + "<br>" +
                     miner.platforms[pi].version + "<br>" + miner.platforms[pi].profile.replace("_", " ").toLowerCase();
            if(miner.platforms[pi].devices.length === 0) {
                var row = document.createElement('tr');
                appHelp.appendCell(row, platDesc);
                var td = appHelp.makeCell('td', 'No devices found in this platform (?).');
                td.colSpan = TABLE_COL_COUNT - 1;
                row.appendChild(td);
                tbody.appendChild(row);
            }
            for(var di in miner.platforms[pi].devices) {
                var device = miner.platforms[pi].devices[di];
                var tr = document.createElement('tr');
                if(di == 0) appHelp.appendCell(tr, platDesc, miner.platforms[pi].devices.length);
                
                var chip = device.type + ', ' + (device.arch? '<strong>' + device.arch + '</strong>' : '');
                chip += '<br><em>';
                chip += device.chip.replace("(tm)", "\u2122").replace("(TM)", "\u2122")
                                   .replace("(r)", "\u00ae").replace("(R)", "\u00ae")
                                   .replace("(c)", "\u00a9").replace("(C)", "\u00a9");
                chip += '</em>'; // + ' (0x' + device.vendorID.toString(16) + ')';
                chip += '<br>' + device.clusters + ' cores @ ' + device.coreClock + ' Mhz';
                appHelp.appendCell(tr, chip);
                
                var global = "RAM: " + Math.floor(device.globalMemBytes / 1024 / 1024) + " MiB";
                var cache = "Cache: " + Math.floor(device.globalMemCacheBytes / 1024) + " KiB";
                var lds = "";
                if(device.lds == 0 || (device.ldsType && device.ldsType == "none")) {}
                else {
                    lds += "Local: " + Math.floor(device.ldsBytes / 1024) + " KiB";
                    if(device.ldsType == "global") lds += "(emu)";
                }
                appHelp.appendCell(tr, global + '<br>' + cache + '<br>' + lds);
                
                // Configuration cell. I temporarely (and very inappropriately) store an handle
                // in the miner representation structure. I will remove it later after being populated.
                device.configCell = appHelp.appendCell(tr, '...');
                
                appHelp.appendCell(tr, '' + linearIndex);
                
                tbody.appendChild(tr);
                miner.platforms[pi].devices[di].linearIndex = linearIndex++;
            }
            
            minerMonitor.requestSimple('configInfo', configInfo_callback);
        }
        
        function quirkyColCount(tbody) {
            var table = tbody.parentNode;
            var count = 5; // because it counts 5 now
            var head = table.getElementsByTagName('THEAD')[0];
            var rows = head.getElementsByTagName('TR');
            for(var check = 0; check < rows.length; check++) {
                var trcount = rows[check].getElementsByTagName('TH').length;
                // colspan?
                if(trcount > count) count = trcount;
            }
            return count;
        }
    }
    
    var numConfigs = 0;
    function configInfo_callback(reply) {
        // "system information" table, "configuration" column
        // put used configs in place and device pattern canvas
        configInfo_deviceMapping(reply);
        minerMonitor.requestSimple('rejectReason', rejectReason_callback);
		
		// configInfo_deviceMapping also sets the algo being mined. This allows me to filter the pools a bit.
		minerMonitor.requestSimple('pools', pools_callback);
        
        // In the meanwhile, let's build the rows of the performance table.
        var tbody = document.getElementById('performance');
        for(var loop = 0; loop < monitorState.usedDevices.length; loop++) {
            var tr = document.createElement('tr');
            var td = document.createElement('td');
            td.innerHTML = monitorState.usedDevices[loop].device.linearIndex + ', ';
            var pattern = window.monitorState.getDevicePattern(monitorState.usedDevices[loop].device.linearIndex);
            td.appendChild(window.appHelp.makeLegendCanvas(pattern, LEGEND_CANVAS_SIZE_PX));
            tr.appendChild(td);
            monitorState.usedDevices[loop].scanTime = genTDAppend(tr, ['min', 'last', 'avg', 'max']);
            monitorState.usedDevices[loop].nonceCount = genTDAppend(tr, ['found', 'bad', 'discarded', 'stale']);
            var slf = document.createElement('td');
            monitorState.usedDevices[loop].lastNonce = {
                when: undefined,
                cell: slf,
            };
            var dsps = document.createElement('td');
            monitorState.usedDevices[loop].dspsCell = dsps;
            tr.appendChild(slf);
            tr.appendChild(dsps);
            tbody.appendChild(tr);
        }
        document.getElementById('totalHashrate').innerHTML = '<em>performance data not yet received</em>';
        window.setInterval(function() {
            var now = new Date();
            for(var loop = 0; loop < window.monitorState.usedDevices.length; loop++) {
                var udev = window.monitorState.usedDevices[loop];
                if(udev.lastNonce.when) udev.lastNonce.cell.textContent = window.appHelp.readableHHHHMMSS(now - udev.lastNonce.when);
            }
        }, 1000);
        minerMonitor.requestStream('scanTime', scanTime_callback);
        minerMonitor.requestStream('deviceShares', deviceShares_callback);
        
        function genTDAppend(container, arr) {
            var ret = {};
            for(var loop = 0; loop < arr.length; loop++) {
                ret[arr[loop]] = {};
                ret[arr[loop]].cell = document.createElement('td');
                ret[arr[loop]].value = null;
                container.appendChild(ret[arr[loop]].cell);
            }
            return ret;
        }
    };
    
    var LEGEND_CANVAS_SIZE_PX = 16;
    function configInfo_deviceMapping(reply) {
        if(reply === null) {
            var bad = '<span class="hugeError">No configurations</span>';
            for(var p = 0; p < window.miner.platforms.length; p++) {
                var plat = window.miner.platforms[p];
                for(var d = 0; d < plat.devices.length; d++) {
                    if(plat.devices[d].configCell) {
                        plat.devices[d].configCell.innerHTML = bad;
                        delete plat.devices[d].configCell;
                    }
                }
            }
            return;
        }
        document.getElementById('algo').innerHTML = reply.algo;
        numConfigs = reply.selected.length;
        var usedDevices = {};
        for(var config = 0; config < reply.selected.length; config++) {
            for(var devSlot = 0; devSlot < reply.selected[config].active.length; devSlot++) {
                var linear = reply.selected[config].active[devSlot].device;
                if(usedDevices[linear] === undefined) usedDevices[linear] = config;
                else {
                    var bad = 'Device ' + linear + ' mapped to [' + usedDevices[linear] + '], ';
                    bad += 'but also found on config [' + config + '].'; // impossible anyway
                    alert(bad);
                    throw bad;
                }
            }
        }
        for(var p = 0; p < window.miner.platforms.length; p++) {
            var plat = window.miner.platforms[p];
            for(var d = 0; d < plat.devices.length; d++) {
                var linear = plat.devices[d].linearIndex;
                if(usedDevices[linear] === undefined) {
                    plat.devices[d].configCell.innerHTML = 'Unused<br>';
                }
                else {
                    var handy = { };
                    handy.device = plat.devices[d];
                    handy.configIndex = usedDevices[linear];
                    window.monitorState.usedDevices.push(handy);
                    plat.devices[d].configCell.textContent = 'Using [' + usedDevices[linear] + '], ';
                }
            }
        }
        for(var p = 0; p < window.miner.platforms.length; p++) {
            var plat = window.miner.platforms[p];
            for(var d = 0; d < plat.devices.length; d++) {
                var devPattern = window.monitorState.getDevicePattern(plat.devices[d].linearIndex);
                var cell = plat.devices[d].configCell;
                var container = cell.parentNode.parentNode.parentNode.parentNode;
                if(devPattern !== undefined) {
                    cell.appendChild(window.appHelp.makeLegendCanvas(devPattern, LEGEND_CANVAS_SIZE_PX));
                    var match = byDeviceIndex(plat.devices[d].linearIndex);
                    plat.devices[d].hashCount = match.hashCount;
                    var details = 'Algorithm implementation: ' + getAlgoImpl(plat.devices[d].linearIndex) + '<br>'
                                + '<span class="specificText">HashCount</span>: ' + match.hashCount;
                    var detailsDiv = window.appHelp.makeMemoryReport(plat.devices[d].linearIndex, details, match.memUsage);
                    var bytesTotal = 0;
                    for(var add = 0; add < match.memUsage.length; add++) {
                        if(match.memUsage[add].space === 'device') bytesTotal += match.memUsage[add].footprint;
                    }
                    container.appendChild(detailsDiv);
                    var summed = document.createTextNode(' ' + window.appHelp.isoDivisorString(bytesTotal, window.appHelp.base2Divisor, 'ceil') + 'B');
                    cell.appendChild(summed);
                    cell.appendChild(document.createElement('br'));
                    cell.appendChild(window.appHelp.makeDetailShowHideButton(detailsDiv, 'Memory usage details'));
                    cell.appendChild(document.createElement('br'));
                }
                //delete plat.devices[d].configCell;
                // not deleted yet, we delete it on probing device reject reasons
            }
        }
        
        function byDeviceIndex(linearDeviceIndex) {
            for(var check = 0; check < reply.selected.length; check++) {
                for(var innerCheck = 0; innerCheck < reply.selected[check].active.length; innerCheck++) {
                    if(reply.selected[check].active[innerCheck].device === linearDeviceIndex) {
                        return reply.selected[check].active[innerCheck];
                    }
                }
            }
            return '[ERROR - impossible]';
        }
        
        function getAlgoImpl(linearDeviceIndex) {
            for(var check = 0; check < reply.selected.length; check++) {
                for(var innerCheck = 0; innerCheck < reply.selected[check].active.length; innerCheck++) {
                    if(reply.selected[check].active[innerCheck].device === linearDeviceIndex) {
                        return reply.selected[check].impl;
                    }
                }
            }
            return '[ERROR - impossible]';
        }
    }
    
	var FAINT_GREEN = '#EEFFEE';
	var FAINT_RED = '#FFEEEE';
	
    function rejectReason_callback(reply) {
        var linearDevices = [];
        var localIndex = [];
        for(var p = 0; p < miner.platforms.length; p++) {
            var plat = miner.platforms[p];
            for(var d = 0; d < plat.devices.length; d++) {
                linearDevices.push(plat.devices[d]);
                localIndex.push({ plat: p, devSlot: d });
            }
        }
        var container = linearDevices[0].configCell.parentNode.parentNode.parentNode;
        for(var loop = 0; loop < reply.length; loop++) {
            var div = window.appHelp.makeFloatingDetailsDiv();
            container.parentNode.appendChild(div);
            linearDevices[loop].configCell.appendChild(window.appHelp.makeDetailShowHideButton(div, "Reject reasons"));
            div.innerHTML += '<h3>Configuration rejections for device ' + loop + '</h3>'
                          + 'Platform ' + localIndex[loop].plat + ' device ' + localIndex[loop].devSlot;
            if(reply[loop].length === 0) div.innerHTML += '<br>Device is eligible to all configurations.<br>';
            //else {
                if(reply[loop].length === numConfigs) div.innerHTML += '<br>Rejected by all configs.<br>';
                var table = document.createElement('table');
                var inner = '<thead><th>Config slot</th><th>Why</th></thead><tbody>';
                for(var cfg = 0; cfg < numConfigs; cfg++) {
                    var find = undefined;
                    for(var match = 0; match < reply[loop].length; match++) {
                        if(reply[loop][match].confIndex === cfg) {
                            find = reply[loop][match];
                            break;
                        }
                    }
                    inner += describe(find, cfg); // d3 here? This is easy...
                }
                table.innerHTML = inner + '</tbody>';
                div.appendChild(table);                
            //}
            div.appendChild(window.appHelp.makeDetailShowHideButton(div, "Close"));
            delete linearDevices[loop].configCell;
        }
        
        function describe(reasons, cfgIndex) {
            var ret = '<tr style="background-color: ';
            ret += reasons === undefined? FAINT_GREEN : FAINT_RED;
            ret += '"><td>' + (reasons? reasons.confIndex : cfgIndex) + '</td><td>';
            if(reasons) {
                ret += '<ul>';
                for(var r = 0; r < reasons.reasons.length; r++) ret += '<li>' + reasons.reasons[r] + '</li>';
                ret += '</ul>';
            }
            else  ret += '\u2713 Eligible';
            return ret + '</td></tr>';
        }
    }
    
    function scanTime_callback(msg) {
        document.getElementById('twindow').textContent = msg.twindow;
        var dev = 0;
        var fields = [ 'min', 'max', 'avg', 'last' ];
        var update = 0;
        for(var loop = 0; loop < msg.measurements.length; loop++) {
            if(msg.measurements[loop] === null) continue;
            for(var check = 0; check < fields.length; check++) {
                var key = fields[check];
                if(msg.measurements[loop][key] === undefined) continue;
                var value = 0 + msg.measurements[loop][key];
                if(window.monitorState.usedDevices[dev].scanTime[key].value !== value) {
                    window.monitorState.usedDevices[dev].scanTime[key].value = value;
                    update++;
                }
            }
            dev++;
        }
        if(update) {
			monitorState.hrGraphScale = refreshPerformanceCells();
			if(!monitorState.perfSamples && monitorState.hrGraphScale.hrTotal) { // Initialize graph, but only if we got performance data.
				var container = document.getElementById('miningStatus');
				var canvas = document.getElementById('perfGraph');
				canvas.style.border = '1px black solid';
				canvas.width = container.clientWidth - 20;
				canvas.height = 300;
				var ctx = canvas.getContext('2d');
				
				monitorState.perfSamples = [];
				monitorState.perfSamples.length = canvas.width; // one pixel, one entry.
				var now = Date.now();
				for(var init = monitorState.perfSamples.length - 1; init + 1; init--) {
					var obj = {};
					obj.x = new Date(now);
					obj.y = [];
					obj.y.length = monitorState.usedDevices.length;
					for(var clear = 0; clear < monitorState.usedDevices.length; clear++) obj.y[clear] = NaN;
					// I want zeros, not undefined-s
					//for(var clear = 0; clear < monitorState.usedDevices.length; clear++) obj.y[clear] = (Math.sin(clear * Math.PI * .15 + init * .0125 * (clear + 1)) * 1234 + 1234)*500;	
					now -= 1000;
					monitorState.perfSamples[init] = obj;
				}
				
				var pattern = [];
				pattern.length = monitorState.usedDevices.length;
				for(var d = 0; d < monitorState.usedDevices.length; d++) {
					pattern[d] = window.monitorState.getDevicePattern(monitorState.usedDevices[d].device.linearIndex);
				}
				
				var hoveringSample = null;
				monitorState.grapher = new StackedSampleChart(monitorState.perfSamples, canvas);
				monitorState.grapher.areaFillStyle = pattern;
				monitorState.grapher.draw();
				
				window.setInterval(function() {
					var obj = {};
					obj.x = new Date();
					obj.y = [];
					obj.y.length = monitorState.usedDevices.length;
					for(var dev = 0; dev < monitorState.usedDevices.length; dev++) {
						var hr = .0;
						var scanTime = monitorState.usedDevices[dev].scanTime.avg.value;
						if(scanTime) {
							hr = 1000.0 / scanTime; // this can still become 0 if pools go down.
							hr *= monitorState.usedDevices[dev].device.hashCount;
						}
						obj.y[dev] = hr;
					}
					monitorState.perfSamples.shift();
					monitorState.perfSamples.push(obj);
					monitorState.grapher.updated(1);
				}, 1000);
			}
		}
    }
    
    function refreshPerformanceCells() {
        var hrTotal = 0;
        var byHashRate = document.getElementById('perfMode').value === 'Hashrate';
        var biggest = {
            hr: 0,
            iso: undefined
        };
        var cells = ['min', 'max', 'avg', 'last'];
        for(var loop = 0; loop < window.monitorState.usedDevices.length; loop++) {
            for(var sub = 0; sub < cells.length; sub++) {
				var mangle = window.monitorState.usedDevices[loop].scanTime[cells[sub]];
                if(!mangle.value) { // == 0 makes a different thing happen. null == 0 -> false, for some reason
                    mangle.cell.textContent = 0;
                    continue;
                }
                var hr = 1000 / mangle.value;
                hr *= window.monitorState.usedDevices[loop].device.hashCount;
                if(sub === 2) hrTotal += hr;
				/* ^ 'last' varies the most, but avg_short is more reliable, especially when
				multiple work queues are dispatched to the same device. */
                var iso = window.appHelp.base10Divisor(hr);            
                var ms = mangle.value;
                if(byHashRate === false) mangle.cell.textContent = ms;
                else if(hr > biggest.hr) {
                    biggest.hr = hr;
                    biggest.iso = iso;
                }
            }
        }
        var header = document.getElementById('perfMeasureHeader');
        if(byHashRate == false) header.textContent = 'Scan time [ms]';
        else {
            header.textContent = 'Hashrate [' + biggest.iso.prefix + 'H/s]';
            for(var loop = 0; loop < window.monitorState.usedDevices.length; loop++) {
                for(var sub = 0; sub < cells.length; sub++) {
					var mangle = window.monitorState.usedDevices[loop].scanTime[cells[sub]];
					if(!mangle.value) continue;
                    var hr = 1000 / mangle.value;
                    hr *= window.monitorState.usedDevices[loop].device.hashCount;
                    hr /= biggest.iso.divisor;
                    var src = cells[sub];
                    if(src === 'max') src = 'min';
                    else if(src === 'min') src = 'max';
                    window.monitorState.usedDevices[loop].scanTime[src].cell.textContent = Math.floor(hr);
                }
            }
        }
        var iso = window.appHelp.base10Divisor(hrTotal);
        hrTotal = Math.floor(hrTotal / iso.divisor);
        document.getElementById('totalHashrate').textContent = hrTotal + ' ' + iso.prefix + 'H/s';
		iso.hrTotal = hrTotal;
		return iso;
    }
    
    function deviceShares_callback(msg) {
        // Quirk: update nonce time if we connected before one was found.
        // The correct way to do this would be to send another 'runtime' command but it's not like this is very useful after all.
        var fnt = document.getElementById('firstNonceTime');
        if(window.miner.firstNonce == undefined) {
            var oldest = 0;
            for(var d = 0; d < msg.lastResult.length; d++) {
                if(msg.lastResult[d] === 0) continue;
                if(oldest === 0 || msg.lastResult[d] < oldest) oldest = msg.lastResult[d];
            }
            if(oldest) {
                window.miner.firstNonce = new Date(oldest * 1000);
                fnt.textContent = window.miner.firstNonce.toLocaleString();
            }
        }
        var name = ['found', 'bad', 'discarded', 'stale'];
        for(var d = 0; d < monitorState.usedDevices.length; d++) {
            var udev = monitorState.usedDevices[d];
            for(var n = 0; n < name.length; n++) {
                var value = msg[name[n]][udev.device.linearIndex];
                if(udev.nonceCount[name[n]].value !== value) {
                    udev.nonceCount[name[n]].value = value;
                    value = '' + value;
                    if(name[n] === 'bad') value = '<span className="hugeError">' + value + '</span>';
                    udev.nonceCount[name[n]].cell.innerHTML = value;
                }
            }
            var last = msg.lastResult[udev.device.linearIndex];
            if(last) udev.lastNonce.when = new Date(last * 1000);
            udev.dspsCell.textContent = msg.dsps[udev.device.linearIndex].toFixed(3);
        }
    }
	
	function pools_callback(arr) {
		monitorState.configuredPools.length = arr.length;
		var container = document.getElementById('configuredPools');
		for(var loop = 0; loop < arr.length; loop++) {
			var tr = document.createElement('tr');
			var rows = arr[loop].users.length || 1;
			appHelp.appendCell(tr, arr[loop].algo, rows);
			appHelp.appendCell(tr, arr[loop].name || '<em>None</em>', rows);
			appHelp.appendCell(tr, arr[loop].url, rows);
			if(arr[loop].users.length === 0) appHelp.appendCell(tr, '<span class="hugeError">No login data</span>'); // not really "huge" as showstopping... but worth attention nonetheless
			else {
				appHelp.appendCell(tr, describeWorkerAuth(arr[loop].users[0]));
				if(arr[loop].users.length > 1) {
					container.appendChild(tr);
					for(var w = 1; w < arr[loop].users.length; w++) {
						tr = document.createElement('tr');
						appHelp.appendCell(tr, arr[loop].users[w]);
						if(w !== arr[loop].users.length - 1) container.appendChild(tr);
					}
				}
			}
			if(arr[loop].algo.toUpperCase() === document.getElementById('algo').textContent.toUpperCase()) {
				tr.style.backgroundColor = FAINT_GREEN;
				monitorState.configuredPools[loop] = {
                    activated: null,
                    numActivations: 0,
                    cumulatedTime: 0,
					
					sent: 0,
                    accepted: 0,
                    rejected: 0,
                    daps: .0,
					
                    lastSubmitReply: null,
                    lastActivity: null,
					
					table: {
						status: null, // yes/no, toggled depending if currently connected or not.
						detailsDiv: null, // I need to update this bgcolor and contents on connect/disconnect of each pool
						sent: null,
						accepted: null,
						rejected: null,
						daps: null,
						lastActivity: null, // child of detailsDiv
						lastSubmitReply: null, // child of detailsDiv
						activated: null, // child of detailsDiv
						numActivations: null, // child of detailsDiv
						cumulatedTime: null, // child of detailsDiv
						
						connLogBody: null, // child of a table child of detailsDiv, we put in a <tr> each observed new connection
						connRow: null // will be object containing handles to 'td' to populate when a connection goes off.
					}
				};
			}
			else monitorState.configuredPools[loop] = null;
			container.appendChild(tr);
		}
		
		var container = document.getElementById('enabledPools');
		var divParent = container.parentNode.parentNode;
		for(var loop = 0; loop < arr.length; loop++) {
			var entry = monitorState.configuredPools[loop];
			if(entry === null) continue;
			var tr = document.createElement('tr');
			var name = arr[loop].name || 'pool[' + loop +']';
			appHelp.appendCell(tr, name);
			var cell = appHelp.appendCell(tr, null);
			entry.table.status = document.createElement('span');
			entry.table.status.innerHTML = '?';
			cell.appendChild(entry.table.status);
			cell.appendChild(document.createTextNode(' '));
			entry.table.detailsDiv = appHelp.makeFloatingDetailsDiv();
			initConnDetailsDiv(entry.table.detailsDiv, name, entry.table);
			divParent.appendChild(entry.table.detailsDiv);
			cell.appendChild(appHelp.makeDetailShowHideButton(entry.table.detailsDiv, 'Connection log'));	
			entry.table.sent = appHelp.appendCell(tr, null);
			entry.table.accepted = appHelp.appendCell(tr, null);
			entry.table.rejected = appHelp.appendCell(tr, null);
			entry.table.daps = appHelp.appendCell(tr, null);		
			container.appendChild(tr);
		}
		minerMonitor.requestStream('poolStats', poolStats_callback);
		
		function initConnDetailsDiv(div, name, handles) {
			div.innerHTML = '<h3>Observed connection status</h3><strong>For pool "' + name + '"</strong><br>';
			var table = document.createElement('table');
			var entries = [
				{ target: 'lastActivity',    desc: 'Last data received' },
				{ target: 'lastSubmitReply', desc: 'Last reply to submit' },
				{ target: 'activated',       desc: 'Last connection' },
				{ target: 'lastConnDown',       desc: 'Last disconnect' },
				{ target: 'numActivations',  desc: 'Activation attempts' },
				{ target: 'cumulatedTime',   desc: 'Cumulated activity time' }
			];
			for(var loop = 0; loop < entries.length; loop++) {
				var tr = document.createElement('tr');
				tr.appendChild(appHelp.makeCell('th', entries[loop].desc + ': '));
				handles[entries[loop].target] = appHelp.appendCell(tr, 'N/A');
				table.appendChild(tr);
			}
			var tconn = document.createElement('table');
			tconn.innerHTML = '<caption>Observed sessions</caption>' + 
			                  '<thead><tr><th>Estabilished</th><th>Disconnected</th><th>Duration</th></tr></thead>'
			handles.connLogBody = document.createElement('tbody');
			tconn.appendChild(handles.connLogBody);
			div.appendChild(table);
			div.appendChild(tconn);
			div.appendChild(appHelp.makeDetailShowHideButton(div, 'Close'));
			
			function addSpan(container, text, br) {
				var span = document.createElement('span');
				span.textContent = text;
				container.appendChild(span);
				if(br) container.appendChild(document.createElement('br'));
				return span;
			}
		}
	}
	
	function describeWorkerAuth(workerData) {
		var ret = '<span class="minerLogin">' + workerData.login + '</span> - ';
		switch(workerData.authorized) {
			case true: ret += '\u2713'; break;
			case false: ret += '<span class="hugeError">failed login!</span>'; break;
			case 'pending': ret += '\u231B'; break;
			case 'inferred': ret += 'ok'; break;
			case 'open': ret += 'not required'; break;
			case 'off': ret += 'inactive'; break;
			default: ret += '<span class="hugeError">Unknown login status!</span>';
		}
		return ret;
	}
	
	function poolStats_callback(arr) {
		for(var loop = 0; loop < arr.length; loop++) {
			if(arr[loop] === null) continue; // no changes till previous
			var entry = monitorState.configuredPools[loop];
			if(entry === null) continue;
			/*^ in practice we might want to look at those things in more detail but as long as live algo-switching
			isn't implemented (it won't be implemented any time soon) the above won't ignore anything unexpected.
			The only exception is the first time this is called as the current miner implementation pulls zero-filled
			objects. It's just fine to ignore them. */
			cupdate(arr[loop], entry, 'sent');
			cupdate(arr[loop], entry, 'accepted');
			cupdate(arr[loop], entry, 'daps', function(value) { return value.toFixed(3) });
			cupdate(arr[loop], entry, 'rejected', function(value) {
				var ret =  '' + value;
				if(value) {
					var frac = value / entry.accepted;
					frac *= 100;
					ret += ' (' + frac.toFixed(2) + '%)';
				}
				return ret;
			});
			if(arr[loop].activated !== undefined && arr[loop].activated != entry.activated) {
				entry.table.status.textContent = arr[loop].activated? '\u2713' : '\u2717';
				entry.table.status.style.color = arr[loop].activated? 'green' : 'red';
				entry.table.detailsDiv.style.backgroundColor = arr[loop].activated? FAINT_GREEN : FAINT_RED;
			}
			cupdate(arr[loop], entry, 'lastActivity', fromSeconds);
			cupdate(arr[loop], entry, 'lastSubmitReply', fromSeconds);
			cupdate(arr[loop], entry, 'numActivations');
			var previously = {
				activated: entry.activated,
				lastConnDown: entry.lastConnDown
			};
			cupdate(arr[loop], entry, 'activated', function(value) {
				return value? fromSeconds(value) : '<em>(Currently inactive)</em>';
			});
			cupdate(arr[loop], entry, 'lastConnDown', function(value) {
				return value? fromSeconds(value) : '<em>(Always worked so far)</em>';
			});
			// Nope. We have a callback for this.
			//cupdate(arr[loop], entry, 'cumulatedTime', function(value) {
			//	return appHelp.readableHHHHMMSS(value * 1000) + ' (' + value + ' seconds)';
			//});
			if(arr[loop].cumulatedTime !== undefined && arr[loop].cumulatedTime !== entry.cumulatedTime) entry.cumulatedTime = +arr[loop].cumulatedTime;
			
			rememberSession(previously, entry);
		}
		
		window.setInterval(refreshCumulated, 1000);
		
		function cupdate(src, state, key, func) {
			if(src[key] === undefined) return;
			var newValue = +src[key];
			if(newValue === state[key]) return;
			state[key] = newValue;
			state.table[key].innerHTML = func? func(newValue) : newValue;
		}
		
		function fromSeconds(seconds) {
			var date = new Date(seconds * 1000);
			return date.toLocaleString();
		}
		
		/* What this does. It monitors connection state from activated and keeps a list of connections around
		which are used to populate a table listing the connections... */
		function rememberSession(was, now) {
			if(was.activated !== now.activated) {
				if(now.activated) {
					if(now.table.connRow !== null)
						alert("Inconsistent state detected, pool went down and I didn't notice."); // "impossible" (emphasis added)
					var tr = document.createElement('tr');
					var activated = new Date(now.activated * 1000);
					appHelp.appendCell(tr, activated.toLocaleString());
					now.table.connRow = {
						disconnected: appHelp.appendCell(tr, '...'),
						elapsed: appHelp.appendCell(tr, '...')
					};
					now.table.connLogBody.appendChild(tr);
				}
				else {
					if(was.activated === null) return;
					//^ this happens when I connect to a miner while the pool is offline.
					
					if(now.table.connRow === null)
						alert("Inconsistent pool going down but no cells to update."); // "impossible" (emphasis added)
					var estabilished = new Date(was.activated * 1000); // I'm pretty positive the logic calling this mantains the value
					var gone = new Date(now.lastConnDown * 1000);
					var elapsed = gone - estabilished;
					now.table.connRow.disconnected.textContent = gone.toLocaleString();
					now.table.connRow.elapsed.textContent = appHelp.readableHHHHMMSS(+elapsed) + ' (' + Math.floor(elapsed / 1000) + ' s)';
					now.table.connRow = null;
				}
			}
			// No need to do that now because activated changes to 0 when connection goes down
			//else if(was.lastConnDown !== now.lastConnDown) {
			//}
		}
		
		function refreshCumulated() {
			for(var loop = 0; loop < monitorState.configuredPools.length; loop++) {
				var entry = monitorState.configuredPools[loop];
				if(entry === null) continue;
				var activated = entry.activated;
				var seconds = entry.cumulatedTime;
				if(activated) {
					activated = new Date(activated * 1000);
					seconds += Math.ceil((Date.now() - activated) / 1000);
				}
				var string = appHelp.readableHHHHMMSS(seconds * 1000) + ' (' + seconds + ' s)';
				if(activated) string = '~' + string;
				if(entry.table.cumulatedTime.textContent !== string) entry.table.cumulatedTime.textContent = string;
			}
		}
	}
};
