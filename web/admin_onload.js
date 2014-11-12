"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

window.server = {
	configFile: undefined, /* I just put there the result of a configFile command. */
	currentConfig: undefined
};

window.wizConfig = {
	algo: undefined,
	poolName: undefined,
	poolURL: undefined,
	workerLogin: undefined,
	workerPass: undefined,
	hwScaledIntensity: undefined,
	targetConfigFile: "init.json"
};

window.easyConfigSteps = [];

var preferredImplementation = {
	qubit: "fiveSteps",
	grsmyr: "monolithic",
	fresh: "warm"
};

		
window.onload = function() {
	window.keepUntilConnect = document.getElementById("administration");
	window.keepUntilConnect.parentNode.removeChild(window.keepUntilConnect);
	
	var wzSteps = document.getElementById("easyConfigSteps");
	for(var loop = 0; loop < wzSteps.childNodes.length; loop++) {
		if(wzSteps.childNodes[loop].tagName !== "DIV") continue;
		easyConfigSteps.push(wzSteps.childNodes[loop]);
	}
	wzSteps.parentNode.removeChild(wzSteps);
	var modifyCurrentConf = document.getElementById("modCurrConf");
	modifyCurrentConf.parentNode.removeChild(modifyCurrentConf);

	function failure(reason) {
		var where = document.getElementById("pre").parentNode;
		var str = "Failed to connect to <span class='specificText'>localhost</span>, ";
		str += "this is a fatal error (" + reason + ")";
		where.innerHTML = str;
	};
	var target = document.getElementById("hostname");
	target.textContent = "localhost";
	
	var callbacks = { };
	callbacks.fail = failure;
	callbacks.close = function() { alert("socket close"); };
	
	var serverHost = "localhost";
	var serverPort = 31001;
	callbacks.success = function() {
		document.body.removeChild(document.getElementById("initializing"));
		document.body.appendChild(window.keepUntilConnect);
		delete window.keepUntilConnect;
		document.getElementById("server").textContent = serverHost + ":" + serverPort;
		window.minerMonitor.requestSimple("configFile", mangleConfigFile);
	};
	callbacks.pingTimeFunc = function(time) { presentation.showReplyTime(time); };
	window.minerMonitor = new MinerMonitor(serverHost, serverPort, "admin", callbacks);
	
	function mangleConfigFile(obj) {
		var where = document.getElementById("configure");
		var msg;
		if(!obj.valid) {
			msg = "<p><span class='hugeError'>Could not load <span class='specificText'>";
			msg += obj.filename + "</span>!</span>";
		}
		else msg = "Loaded <span class='specificText'>" + obj.filename + "</span>.";
		if(obj.explicit) msg += "<br>File explicitly specified by command line.";
		else if(obj.redirected) msg += "<br>File redirected by JSON annotation.";
		else msg += "<br>(default configuration file)";
		msg += "</p>";
		window.server.configFile = obj;
		where.innerHTML = msg;
		minerMonitor.requestSimple("getRawConfig", mangleConfigProbing);
	}
	
	function startEasyConfigWizard() {
		// For the easy config wizard, I take it easy! I remove everything which is not really necessary.
		minerMonitor.callbacks.pingTimeFunc = undefined;
		var where = document.getElementById("administration");
		where.innerHTML = "";
		where.appendChild(window.easyConfigSteps[0]);
	}
	function mangleConfigProbing(reply) {
		var where = document.getElementById("configure");
		var msg = "";
		var btn;
		if(!window.server.configFile.valid) {
			if(reply.raw.length === 0) {
				msg += "<p>Looks like the file couldn't be loaded.<br>";
				msg += "If running for the first time, this is normal.<br>Use the button below to create a new default configuration file.</p>"
				btn = document.createElement("button");
				btn.textContent = "New configuration wizard";
				compatible.setEventCallback(btn, "click", startEasyConfigWizard);
			}
			else {
				msg += "<p>Error " + reply.errorCode + " @ " + reply.errorOffset + ": " + reply.errorDesc + "<br>";
				if(reply.errorOffset < reply.raw.length) {
					msg += "<span class='specificText'>";
					var normalized = "";
					var ehw = 5; // error half width, in characters.
					for(var c = 0; c < reply.errorOffset - ehw; c++) normalized += normalChar(reply.raw[c]);
					normalized += "<span class='hugeError'>";
					for(var c = reply.errorOffset - ehw; c < reply.errorOffset - 1; c++) normalized += normalChar(reply.raw[c]);
					normalized += "<strong>" + normalChar(reply.raw[reply.errorOffset - 1]) + "</strong>";
					for(var c = reply.errorOffset; c < Math.min(reply.errorOffset + ehw, reply.raw.length); c++) normalized += normalChar(reply.raw[c]);
					normalized += "</span>";
					for(var c = reply.errorOffset + ehw; c < reply.raw.length; c++) normalized += normalChar(reply.raw[c]);
					msg += normalized;
					msg += "</span><br>";
					msg += "File contains synctactic errors (first highlighted above). This cannot happen if you use the automated administration tools.<br>";
					msg += "I take for granted you fiddled with the file manually. I'll leave you fix the problem by yourself.<br>";
				}
				else {
					msg += "&lt;File is too big to be displayed correctly!&gt;<br>This is very likely a real problem.<br>";
					msg += "You'll have to <em>Open user app folder</em> (from miner menu) and fiddle with the files by yourself.<br>";
				}
				msg += "</p>";
			}
		}
		else {
			window.server.currentConfig = reply.configuration;
			if(reply.errors) {
				msg += "<p>Errors found:<ol>";
				for(var loop = 0; loop < reply.errors.length; loop++) msg += "<li>" + reply.errors[loop] + "</li>";
				msg += "</ol></p>";
			}
			btn = document.createElement("button");
			btn.textContent = "Modify current configuration";
			compatible.setEventCallback(btn, "click", startModifyConfig);
		}
		where.innerHTML += msg;
		if(btn) where.appendChild(btn);
	}
	
	function normalChar(c) {
		switch(c) {
		case ' ': return "&nbsp;";
		case '\t': return "&nbsp;&nbsp;&nbsp;&nbsp;";
		case '\n': return "<br>";
		}
		return c;
	}
	
	function startModifyConfig() {
		minerMonitor.callbacks.pingTimeFunc = undefined;
		document.body.removeChild(document.getElementById("administration"));
		document.body.appendChild(modifyCurrentConf);
		window.minerMonitor.requestSimple("systemInfo", function(obj) {
			var gpu = eligibleGPUListFromPlatformArray(obj.platforms);
			var slowest = 0;
			var lowestPerf = gpu[0].coreClock * gpu[0].clusters;
			for(var test = 1; test < gpu.length; test++) {
				var estimate = gpu[test].coreClock * gpu[test].clusters;
				if(estimate < lowestPerf) {
					slowest = test;
					lowestPerf = estimate;
				}
			}
			window.server.lowestParallelClocks = lowestPerf;
			minerMonitor.requestSimple("getRawConfig", getCurrentConfigValues);
		});
	}
	
	function getCurrentConfigValues(reply) {
		window.server.currentConfig = reply.configuration;
		getOptionIndex(document.getElementById("modAlgo"), reply.configuration.algo).selected = true;
		var pool = singlePoolRequired(reply.configuration.pools);
		if(pool.url) document.getElementById("modURL").value = pool.url;
		if(pool.user) document.getElementById("modLogin").value = pool.user;
		if(pool.pass) document.getElementById("modPass").value = pool.pass;
		if(pool.nick) document.getElementById("modPoolName").value = pool.nick;
		// The current JSON protocol is  bit broken. On the pro side, it allows the linearIntensity to be pulled out very easily.
		// If the li is a valid range value then use the range control. Otherwise, use the input number element.
		var linearIntensity = reply.configuration.implParams[reply.configuration.algo][reply.configuration.impl].linearIntensity;
		var range = document.getElementById("hwScaledInt");
		var ratio = window.server.lowestParallelClocks / reference_parallelGPUClocks();
		var hwsi = linearIntensity / ratio / reference_linearIntensity();
		hwsi *= 100;
		var k = hwsi / range.step;
		var destroy;
		if(hwsi > range.max || hwsi < range.min || k != Math.floor(k)) {
			destroy = document.getElementById("optModHWSI");
			document.getElementById("modLinearIntensity").value = linearIntensity;
		}
		else {
			destroy = document.getElementById("optModLI");
			range.value = hwsi;
		}
		destroy.parentNode.removeChild(destroy);
	}
	
	
	function getOptionIndex(sel, value) {
		for(var loop = 0; loop < sel.length; loop++) {
			if(sel.item(loop).tagName === "OPTION") { // sel[loop]
				if(sel.item(loop).value === value) return loop;
			}
		}
		throw "Option having value \"" + value + "\" not present in passed " + sel.tagName + " control.";
	}
	
	// This will have to go in the future! Main problem is objects don't guarantee order.
	// To be switched back to array, most likely to be used on top.
	function singlePoolRequired(allp) {
		var found;
		for(var key in allp) {
			if(found === undefined) {
				found = allp[key];
				found.nick = key;
			}
			else throw "More than one pool found in the .pools object, results undefined.";
		}
		return found;
	}
};


function eligibleGPUListFromPlatformArray(plat) {
	var gpu = [];
	for(var p = 0; p < plat.length; p++) {
		for(var d = 0; d < plat[p].devices.length; d++) {
			var device = plat[p].devices[d];
			if(device.type !== "GPU") continue;
			var cmp = "Graphics Core Next 1.";
			if(device.arch.substr(0, cmp.length) !== cmp) continue;
			gpu.push(device);				
		}
	}
	return gpu;
}


function reference_parallelGPUClocks() {
	// The reference is my Radeon 7750 Capeverde.
	// It got 8 clusters @ 850 Mhz
	return 8 * 850;
}


function reference_linearIntensity() {
	return 512; // Fairly smooth
}

function modConfigSaveAndReboot() {
	var linearIntensity;
	var hwsi = document.getElementById("hwScaledInt");
	if(hwsi) {
		var ratio = window.server.lowestParallelClocks / reference_parallelGPUClocks();
		linearIntensity = reference_linearIntensity() * hwsi.valueAsNumber / 100;
		linearIntensity = Math.floor(linearIntensity * ratio);
	}
	else {
		linearIntensity = document.getElementById("modLinearIntensity").value;
	}
	linearIntensity = 1 * linearIntensity;
	var algo = document.getElementById("modAlgo").value;
	var algoFamilies = {};
	algoFamilies[algo] = {};
	algoFamilies[algo][preferredImplementation[algo]] = {};
	algoFamilies[algo][preferredImplementation[algo]].linearIntensity = linearIntensity;
	var cmd = {
		command: "saveRawConfig",
		params: {
			destination: window.server.configFile.filename,
			configuration: {
				pools: [],
				driver: "OCL",
				algo: algo,
				impl: preferredImplementation[algo],
				checkNonces: false,
				implParams: algoFamilies
			}
		}
	};
	var poolName = document.getElementById("modPoolName").value;
	if(!poolName) {
		alert("For the time being, pools must have a name.");
		return;
	}
	cmd.params.configuration.pools[0] = {
		url: stripProtocol(document.getElementById("modURL").value),
		user: document.getElementById("modLogin").value,
		pass: document.getElementById("modPass").value,
		coinDiffMul: coinDiffByAlgo(algo),
		algo: algo,
		protocol: "stratum",
		name: poolName
	};
	minerMonitor.request(cmd, function(reply) { 
		var parent = document.getElementById("modCurrConf");
		serverCFGSaved(reply, parent);
		var dis = [ "modAlgo", "modURL", "modLogin", "modPass", "modPoolName", "hwScaledInt", "modLinearIntensity", "modButton" ];
		for(var loop = 0; loop < dis.length; loop++) {
			var control = document.getElementById(dis[loop]);
			if(control) control.disabled = true;
		}
	});
	
}
