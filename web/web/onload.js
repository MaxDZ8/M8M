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
	callbacks.success = function() { // ok, websocket communication is up... but is the correct application version?
		minerMonitor.requestSimple("version", function(gotVer) {
			var wanted = 3;
			if(gotVer !== wanted) {
				failure("Version mismatch. Got " + gotVer + " but must be " + wanted);
				window.minerMonitor.socket.close();
				delete window.minerMonitor;
			}
			else {
				callbacks.pingTimeFunc = function(time) { presentation.showReplyTime(time); };	
	
				document.body.removeChild(document.getElementById("initializing"));
				// todo: populate information from server first? 
				document.body.appendChild(window.keepUntilConnect);
				delete window.keepUntilConnect;
				
				document.getElementById("server").textContent = serverHost + ":" + serverPort;
				window.minerMonitor.requestSimple("uptime", uptimeCallback);
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
				
				window.minerMonitor.requestSimple("systemInfo", mangleSysInfo);
			}
		});		
		
		
		function mangleSysInfo(pdesc) {
			presentation.appendDevicePlatforms(pdesc);
			window.minerMonitor.requestSimple("algo", function(reply) {
				presentation.updateMiningElements(reply);
				window.minerMonitor.requestSimple("pools", function(reply) {
					var authWarning = document.getElementById("poolAuthWarning");
					
					if(!document.getElementById("poolName")) { // first time, generate
						var div = document.createElement("div");
						compatible.modClass(div, "detailsBox", "add");
						div.style.display = "none";
						div.style.zIndex = 11;
						var string = "<h3>Pool details</h3>";
						string += "<strong>Name:</strong> <span id='poolName'></span><br>";
						string += "<strong>URL:</strong> <span id='poolURL'></span><br>";
						string += "<strong>Login:</strong> <span id='user'></span><br>";
						string += "<strong>Authentication status:</strong> <span id='authStatus'></span><br>";
						div.innerHTML = string;
						document.getElementById("miningStatus").appendChild(div);
						var shbutton = presentation.support.makeDetailShowHideButton(div, "Close");
						compatible.setEventCallback(document.getElementById("poolDetails"), "click", function(click) {
							presentation.support.showHideBTNFunc(this, div, click);
						});
						div.appendChild(shbutton);
						presentation.support.makeMovable(div);
					}
		
					if(reply.length) presentation.updatePoolElements(reply[0]);
					else {
						var purl = document.getElementById("poolURL");
						var user = document.getElementById("user");
						var authWarning = document.getElementById("poolAuthWarning");
						var poolName = document.getElementById("poolName");
						var authStatus = document.getElementById("authStatus");
						purl.textContent = poolName.textContent = "!! not connected !!";
						user.innerHTML = authStatus.innerHTML = "!! not connected !!";
						document.getElementById("pool").textContent = "";
						authWarning.innerHTML = "<strong>!! not connected !!</strong>";
						compatible.modClass([authWarning, user, authStatus, poolName, purl], "hugeError", "add");
					}
					
					/*! \todo This will have to be moved a day so updatePoolElements will work on stored state instead! */
					server.remote = reply;
					
					window.minerMonitor.requestSimple("deviceConfig", function(configInfo) {
						presentation.updateConfigElements(configInfo);
						window.minerMonitor.requestSimple("rejectReason", function(rejInfo) {
							presentation.updateRejectElements(rejInfo);
							fillConfigTable();
						});
					});		
					window.minerMonitor.requestStream({ command: "poolShares" }, function(pinfo) {
						for(var loop = 0; loop < pinfo.length; loop++) {
							var src = pinfo[loop];
							var dst = window.server.remote[loop];
								//! \todo multi-worker support
								if(dst.shares === undefined) {
									dst.shares = {};
									dst.shares.accepted = 0;
									dst.shares.rejected = 0;
									dst.shares.sent = 0;
								}
								if(src.sent !== undefined) dst.shares.sent = src.sent;
								if(src.accepted !== undefined) dst.shares.accepted = src.accepted;
								if(src.rejected !== undefined) dst.shares.rejected = src.rejected;
						}
						presentation.refreshPoolWorkerStats();
					});
				});
			});
		};
	};
	window.minerMonitor = new MinerMonitor(serverHost, serverPort, "monitor", callbacks);
	
	function fillConfigTable() {
		var cmd = {
			command: "configInfo",
			params: ["hashCount", "memUsage"]
		};
		window.minerMonitor.request(cmd, function(cinfo, ptime) {
			presentation.updateConfigInfo(cinfo);
			var active = 0;
			var measurementNames = ["min", "last", "avg", "max"];
			for(var loop = 0; loop < server.hw.linearDevice.length; loop++) {
				if(server.hw.linearDevice[loop].visPattern === undefined) {
					presentation.hashTimeCells.push(null);
					continue;
				}
				server.hw.linearDevice[loop].lastPerf = {};
				var row = document.getElementById("configInfo").childNodes[active++];
				var mapping = {};
				for(var gen = 0; gen < measurementNames.length; gen++) {
					var key = measurementNames[gen];
					mapping[key] = document.createElement("td");
					row.appendChild(mapping[key]);
				}
				presentation.hashTimeCells.push(mapping);
			}
			window.minerMonitor.requestStream({ command: "scanTime" }, function(obj) {
				var how = presentation.perfMode;
				document.getElementById("twindow").textContent = obj.twindow;
				for(var loop = 0; loop < server.hw.linearDevice.length; loop++) { // same length as obj.measurements
					var device = server.hw.linearDevice[loop];
					if(device.configIndex === undefined) continue; // corresponding entry is null
					for(var cp = 0; cp < measurementNames.length; cp++) {
						var newValue = obj.measurements[loop][measurementNames[cp]];
						if(newValue !== undefined) device.lastPerf[measurementNames[cp]] = newValue;
					}
				}
				var niceHR = presentation.refreshDevicePerf();
				var header = document.getElementById("perfMeasureHeader");
				if(how === "itime") header.textContent = "Scan time [ms]";
				else header.textContent = "Hashrate [" + niceHR.prefix + "H/s]";
			});
			
			active = 0;
			for(var loop = 0; loop < server.hw.linearDevice.length; loop++) {
				if(server.hw.linearDevice[loop].visPattern === undefined) {
					presentation.noncesCells.push(null);
					continue;
				}
				var row = document.getElementById("configInfo").childNodes[active++];
				var names = [ "found", "bad", "discarded", "stale", "lastResult", "dsps" ];
				mapping = {};
				for(var gen = 0; gen < names.length; gen++) {
					var key = names[gen];
					mapping[key] = document.createElement("td");
					row.appendChild(mapping[key]);
				}
				presentation.noncesCells.push(mapping);
			}
			window.minerMonitor.requestStream({ command: "deviceShares" }, presentation.refreshDeviceShareStats);
			
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
