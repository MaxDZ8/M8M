"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

window.server = {
	hw: {
		platforms: [],
		linearDevice: []
	},
	config: [],
	getDevicesUsingConfig: function(conf) {
		var match = [];
		var plat = this.hw.platforms;
		for(var p = 0; p < plat.length; p++) {
			for(var d = 0; d < plat[p].devices.length; d++) {
				var device = plat[p].devices[d];
				if(device.configIndex !== undefined && device.configIndex == conf) match.push(device);
			}
		}
		return match;
	}	
};
		
		
window.onload = function() {
	
	window.keepUntilConnect = document.getElementById("monitoring");
	window.keepUntilConnect.parentNode.removeChild(window.keepUntilConnect);

	var CONNECTION_TIMEOUT = 15;
	var points = 0, ticked = 0;
	var maxPoints = 3;
	function failure(reason) {
	//! todo: find a way for better error messaging?
		document.getElementById("pre").textContent = "Failed to connect to";
		document.getElementById("advancing").textContent = ", this is a fatal error (" + reason + ")";
		var bar = document.getElementById("waitHint");
		if(bar) bar.parentNode.removeChild(bar);
	};
	var target = document.getElementById("hostname");
	target.textContent = "localhost";
	
	var callbacks = { };
	callbacks.fail = failure;
	
	var serverHost = "localhost";
	var serverPort = 31000;
	callbacks.close = function() { alert("socket close"); };
	callbacks.success = function() {
		document.body.removeChild(document.getElementById("initializing"));
		// todo: populate information from server first? 
		document.body.appendChild(window.keepUntilConnect);
		delete window.keepUntilConnect;
		
		document.getElementById("totalHashrate").textContent = "... H/s";
		
		compatible.setEventCallback(document.getElementById("perfMode"), "change", function(ev) {
			var how = this.selectedOptions[0].id;
			if(how === "perfMode-HR") presentation.perfMode = "hashrate";
			else presentation.perfMode = "itime";
			var niceHR = presentation.refreshDevicePerf();			
			var header = document.getElementById("perfMeasureHeader");
			if(how === "perfMode-IT") header.textContent = "Scan time [ms]";
			else {
				header.textContent = "Hashrate [" + niceHR.prefix + "H/s]";
			}
		});
		
		window.request_system(minerMonitor, function(pdesc) {
			presentation.appendDevicePlatforms(pdesc);
			window.minerMonitor.requestSimple("algo", function(reply) {
				presentation.updateMiningElements(reply);
				window.minerMonitor.requestSimple("pool", function(poolinfo) {
					presentation.updatePoolElements(poolinfo);
					
					/*! \todo This will have to be moved a day so updatePoolElements will work on stored state instead! */
					server.remote = {};
					if(poolinfo.name) {
						server.remote[poolinfo.name] = {};
						server.remote[poolinfo.name].url = poolinfo.url;
						server.remote[poolinfo.name].worker = [];
						var w = server.remote[poolinfo.name].worker;
						for(var init = 0; init < poolinfo.users.length; init++) {
							var build = {};
							build.sent = 0;
							build.accepted = 0;
							build.rejected = 0;
							build.name = poolinfo.users[init].login;
							w.push(build);
						}
					}
					
					window.minerMonitor.requestSimple("deviceConfig", function(configInfo) {
						presentation.updateConfigElements(configInfo);
						window.minerMonitor.requestSimple("rejectReason", function(rejInfo) {
							presentation.updateRejectElements(rejInfo);
							fillConfigTable();
						});
					});
					var cmd = {
						command: "poolShares",
						enable: true,
					};					
					window.minerMonitor.requestStream(cmd, function(pinfo) {
						for(var key in pinfo) {
							if(key === "pushing") continue; // ugly, I should really figure out a better protocol
							var src = pinfo[key];
							var dst = window.server.remote[key];
							for(var cp = 0; cp < src.sent.length; cp++) {
								dst.worker[cp].sent = src.sent[cp];
								dst.worker[cp].accepted = src.accepted[cp];
								dst.worker[cp].rejected = src.rejected[cp];
							}
						}
						presentation.refreshPoolWorkerStats();
					});
				});
			});
		});
		document.getElementById("server").textContent = serverHost + ":" + serverPort;
		window.minerMonitor.requestSimple("uptime", uptimeCallback);
	};
	callbacks.pingTimeFunc = function(time) { presentation.showReplyTime(time); };
	window.minerMonitor = new MinerMonitor(serverHost, serverPort, "monitor", callbacks);
	
	function fillConfigTable() {
		var cmd = {
			command: "configInfo",
			params: ["hashCount", "memUsage"]
		};
		window.minerMonitor.request(cmd, function(cinfo, ptime) {
			presentation.updateConfigInfo(cinfo);
			var request = {
				command: "scanTime",
				devices: [],
				requesting: {}
			};
			var active = 0;
			for(var loop = 0; loop < server.hw.linearDevice.length; loop++) {
				if(server.hw.linearDevice[loop].visPattern === undefined) {
					presentation.hashTimeCells.push(null);
					continue;
				}
				// ^ stupid, nonsensical way to say the device is not used so don't send us stats.
				server.hw.linearDevice[loop].lastPerf = {};
				var row = document.getElementById("configInfo").childNodes[active++];
				var names = ["last", "min", "slrAvg", "slidingAvg", "max"];
				var mapping = {};
				for(var gen = 0; gen < names.length; gen++) {
					var key = names[gen];
					mapping[key] = document.createElement("td");
					row.appendChild(mapping[key]);
					request.requesting[key] = true;
				}
				presentation.hashTimeCells.push(mapping);
				request.devices.push(loop);
			}
			window.minerMonitor.requestStream(request, function(obj) {
				var how = presentation.perfMode;
				presentation.refreshPerfHeaders(obj);
				var names = ["last", "min", "slrAvg", "slidingAvg", "max"];
				for(var loop = 0; loop < server.hw.linearDevice.length; loop++) {
					var device = server.hw.linearDevice[loop];
					if(device.configIndex === undefined) continue;
					for(var cp = 0; cp < names.length; cp++) {
						var newValue = obj.measurements[loop][names[cp]];
						if(newValue !== undefined) device.lastPerf[names[cp]] = newValue;
					}
				}
				var niceHR = presentation.refreshDevicePerf();
				var header = document.getElementById("perfMeasureHeader");
				if(how === "itime") header.textContent = "Scan time [ms]";
				else header.textContent = "Hashrate [" + niceHR.prefix + "H/s]";
			});
			
			request = {
				command: "deviceShares",
				devices: []
			};
			active = 0;
			for(var loop = 0; loop < server.hw.linearDevice.length; loop++) {
				if(server.hw.linearDevice[loop].visPattern === undefined) {
					presentation.noncesCells.push(null);
					continue;
				}
				request.devices.push(loop);
				var row = document.getElementById("configInfo").childNodes[active++];
				names = [ "good", "bad", "stale", "lastResult" ];
				mapping = {};
				for(var gen = 0; gen < names.length; gen++) {
					var key = names[gen];
					mapping[key] = document.createElement("td");
					row.appendChild(mapping[key]);
				}
				presentation.noncesCells.push(mapping);
			}
			window.minerMonitor.requestStream(request, presentation.refreshDeviceShareStats);
			
			presentation.resultTimeElapsedRefresh = window.setInterval(function() {
				presentation.refreshResultTimeElapsed();
			}, 1000);
		});
	}
	
	function uptimeCallback(reply) {
		window.server.startTime = reply;
		presentation.makeUptimeInfo();
	}
};
