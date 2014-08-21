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
	var animatePoints = window.setInterval(ticking, 1000);
	function failure(reason) {
	//! todo: find a way for better error messaging?
		document.getElementById("pre").textContent = "Failed to connect to";
		document.getElementById("advancing").textContent = ", this is a fatal error (" + reason + ")";
		var bar = document.getElementById("waitHint");
		if(bar) bar.parentNode.removeChild(bar);
		window.clearInterval(animatePoints);
	};
	function ticking() {
		points %= maxPoints;
		var periods = "";
		for(var i = 0; i <= points; i++) periods += ".";
		document.getElementById("advancing").textContent = periods;
		points++;
		ticked++;
		if(ticked > CONNECTION_TIMEOUT) failure("Connection timed out");
	}
	var target = document.getElementById("hostname");
	target.textContent = "localhost";
	
	var callbacks = { };
	callbacks.fail = failure;
	
	var serverHost = "localhost";
	var serverPort = 31000;
	callbacks.success = function() {
		window.clearInterval(animatePoints);
		document.body.removeChild(document.getElementById("initializing"));
		// todo: populate information from server first? 
		document.body.appendChild(window.keepUntilConnect);
		delete window.keepUntilConnect;
		window.request_system(minerMonitor, function(pdesc, pingTime) {
			presentation.appendDevicePlatforms(pdesc, pingTime);
			window.minerMonitor.requestSimple("algo?", function(reply, pingTime) {
				presentation.updateMiningElements(reply, pingTime);
				window.minerMonitor.requestSimple("currentPool?", function(poolinfo, pingTime) {
					presentation.updatePoolElements(poolinfo, pingTime);
					window.minerMonitor.requestSimple("deviceConfig?", function(configInfo, pingTime) {
						presentation.updateConfigElements(configInfo, pingTime);
						window.minerMonitor.requestSimple("whyRejected?", function(rejInfo, pingTime) {
							presentation.updateRejectElements(rejInfo, pingTime);
							fillConfigTable();
						});
					})
				});
			});
		});
		document.getElementById("server").textContent = serverHost + ":" + serverPort;
	};
	callbacks.pingTimeFunc = function(time) { presentation.showReplyTime(time); };
	window.minerMonitor = new MinerMonitor(serverHost, serverPort, callbacks);
	
	function fillConfigTable() {
		var cmd = {
			command: "configInfo?",
			params: ["hashCount", "memUsage"]
		};
		window.minerMonitor.request(cmd, function(cinfo, ptime) {
			presentation.updateConfigInfo(cinfo);
			var request = {
				command: "scanTime?",
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
				var row = document.getElementById("configInfo").childNodes[active++];
				var names = ["last", "min", "savg", "lavg", "max"];
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
			window.minerMonitor.requestStream(request, presentation.refreshIterationTime);
			
			request = {
				command: "deviceShares?",
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
				names = [ "good", "bad", "stale" ];
				mapping = {};
				for(var gen = 0; gen < names.length; gen++) {
					var key = names[gen];
					mapping[key] = document.createElement("td");
					row.appendChild(mapping[key]);
				}
				presentation.noncesCells.push(mapping);
			}
			window.minerMonitor.requestStream(request, presentation.refreshDeviceShareStats);
			request = {
				command: "profiler?",
				what: [ "iterationTime" ]
			};
			window.minerMonitor.requestStream(request, presentation.refreshShareRateGraph);
			presentation.makeCanvasGraph();
		});
	}
};
