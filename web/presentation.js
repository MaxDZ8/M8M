"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
 /* This file contains various helper functions closely bound to HTML source.
They manipulate the DOM tree and respond to various messages from server. 
...
Mostly presentation. It comes handy to evolve state as well. */

var presentation = {
	configCells: [],
	noteCells: [],
	hashTimeCells: [], // one entry for each device, no matter if enabled or not, by linear index
	noncesCells: [],
	perfMode: "itime", // if "iteration time" show performance in milliseconds, else ("hashrate") show in khs/mhs/whatever
	
	showReplyTime: function(pings) {
		document.getElementById("ping").textContent = Math.ceil(pings * 1000);
	},

	appendDevicePlatforms: function(pdesc) {
		document.getElementById("M8M_API").textContent = pdesc.API;
		var sysInfoCont = document.getElementById("sysInfoDevices");
		var linearIndex = 0;
		presentation.configCells = [];
		presentation.noteCells = [];
		for(var i = 0; i < pdesc.platforms.length; i++) {
			var row = document.createElement("tr");
			var cell = document.createElement("td");
			var platform = pdesc.platforms[i];
			cell.rowSpan = Math.max(platform.devices.length, 1);
			cell.innerHTML = "<em>" + platform.name + "</em><br>by " +
							 platform.vendor + "<br>" +
							 platform.version + "<br>" + platform.profile.replace("_", " ").toLowerCase();
			row.appendChild(cell);
			for(var d = 0; d < pdesc.platforms[i].devices.length; d++) {
				if(d) row = document.createElement("tr");
				pdesc.platforms[i].devices[d].linearIndex = linearIndex++;
				pdesc.platforms[i].devices[d]
				describeDevice(row, pdesc.platforms[i].devices[d]);
				sysInfoCont.appendChild(row);
				server.hw.linearDevice.push(pdesc.platforms[i].devices[d]);
			}
			if(pdesc.platforms[i].devices.length == 0) {
				var whoops = document.createElement("td");
				whoops.colSpan = 5;
				whoops.textContent = "No devices found in this platform (?).";
				row.appendChild(whoops);
				sysInfoCont.appendChild(row);
			}
		}
		var numDevice = 0;
		for(var p = 0; p < pdesc.platforms.length; p++) {
			for(var d = 0; d < pdesc.platforms[p].devices.length; d++) {
				pdesc.platforms[p].devices[d].linearIndex = numDevice;
				numDevice++;
			}
		}
		pdesc.linearDevice = [];
		for(var p = 0; p < pdesc.platforms.length; p++) {
			for(var d = 0; d < pdesc.platforms[p].devices.length; d++) {
				pdesc.linearDevice.push(pdesc.platforms[p].devices[d]);
			}
		}
		window.server.hw = pdesc;
		
		/*!
		\param yourLine A <tr> element to push <td>
		\param device CLDeviceDesc object. */
		function describeDevice(yourLine, device) {
			var typology = device.type + (device.arch? ', ' + '<strong>' + device.arch + '</strong>' : '');
			var chipDetail = '<em>' + device.chip.replace("(tm)", "\u2122") + '</em>'; // + ' (0x' + device.vendorID.toString(16) + ')';
			var chipPower = device.clusters + ' cores @ ' + device.nclock + ' Mhz';
			addCell(typology + '<br>' + chipDetail + '<br>' + chipPower);
			
			var global = "RAM: " + Math.floor(device.memory / 1024 / 1024) + " MiB";
			var cache = "Cache: " + Math.floor(device.cache / 1024) + " KiB";
			var lds = "";
			if(device.lds == 0 || (device.ldsType && device.ldsType == "none")) {}
			else {
				lds += "Local: " + Math.floor(device.lds / 1024) + " KiB";
				if(device.ldsType == "global") lds += "(emu)";
			}
			addCell(global + '<br>' + cache + '<br>' + lds);
			presentation.configCells.push(addCell("..."));
			presentation.noteCells.push(addCell("..."));
			addCell(device.linearIndex);
			
			function addCell(text) {
				var c = document.createElement("td");
				c.innerHTML = text;
				yourLine.appendChild(c);
				return c;
			}
		}
	},


	updateMiningElements: function(reply) {
		var algo = document.getElementById("algo");
		var impl = document.getElementById("implementation");
		var version = document.getElementById("implVersion");
		if(!reply.algo) {
			compatible.modClass([algo, impl, version], "hugeError", "add");
			algo.textContent = "!! nothing !!";
			impl.textContent = "!! N/A !!";
			version.textContent = "!! N/A !!";
		}
		else if(!reply.impl || !reply.version) { // in general, either both or none will be defined
			compatible.modClass(algo, "hugeError", "remove");
			compatible.modClass([impl, version], "hugeError", "add");
			algo.innerHTML = reply.algo;
			impl.textContent = "!! ??? !!";
			version.textContent = "!! ??? !!"; // will likely never happen	
		}
		else {
			compatible.modClass([algo, impl, version], "hugeError", "remove");
			algo.innerHTML = reply.algo;
			impl.textContent = reply.impl;
			version.textContent = reply.version;
		}
	},


	updatePoolElements: function(object, pingTime) {
		var pool = document.getElementById("pool");
		var purl = document.getElementById("poolURL");
		var user = document.getElementById("user");
		if(object.name === undefined) {
			compatible.modClass([algo, impl, version], "hugeError", "add");
			pool.textContent = "!! nothing !!";
			purl.textContent = "!! N/A !!";
			user.textContent = "!! N/A !!";		
		}
		else {
			pool.textContent = object.name;
			purl.textContent = object.url;
			compatible.modClass(purl, ["notSoImportant"], "add");
			if(object.users === undefined || object.users.length == 0) { // 0, undefined, all the same
				compatible.modClass(user, "hugeError", "add");
				user.textContent = "!! no workers found !!";
			}
			else {
				while(user.lastChild) user.removeChild(user.lastChild);
				var len = object.users.length;
				for(var i = 0; i < len; i++) {
					if(i) {
						var separate = document.createTextNode();
						separate.textContent = ", ";
						user.appendChild(separate);
					}
					user.appendChild(describeWorker(object.users[i]));
				}
			}
		}
		
		function describeWorker(worker) {
			var el = document.createElement("span");
			compatible.modClass(el, ["specificText", "minerLogin"], "add");
			el.textContent = worker.login;
			if(!worker.authorized) {
				compatible.modClass(el, "hugeError", "add");
				var note = document.createElement("span");
				compatible.modClass(note, "notSoImportant", "add"); // well, that's very important indeed but it's a less important detail of an huge error!
				note.textContent = " (failed authorization)";
				el.appendChild(note);
			}
			return el;		
		}
	},
	
	updateConfigElements: function(devConfs) {
		var tbody = document.getElementById("sysInfoDevices");
		var used = 0;
		for(var loop = 0; loop < devConfs.length; loop++) {
			var cell = presentation.configCells[loop];
			if(devConfs[loop] === "off") cell.innerHTML = "Not used";
			else {
				cell.textContent = "Using [" + devConfs[loop] + ']';
				used++;
			}
		}
		var usePatterns = false;
		var thumbs = presentation.patterns(20, used, usePatterns? { glyphs: presentation.glyphs } : undefined);
		compatible.modClass(thumbs, "devicePattern", "add");
		var take = 0;
		for(var loop = 0; loop < server.hw.platforms.length; loop++) {
			for(var inner = 0; inner < server.hw.platforms[loop].devices.length; inner++) {
				var device = server.hw.platforms[loop].devices[inner];
				if(devConfs[device.linearIndex] === "off") continue;
				device.visPattern = thumbs[take];
				device.configIndex = devConfs[device.linearIndex];
				
				var separator = document.createTextNode(",");
				presentation.configCells[take].appendChild(separator);
				presentation.configCells[take].appendChild(document.createElement("br"));
				presentation.configCells[take].appendChild(thumbs[take]);
				take++;				
			}
		}
	},
	
	updateRejectElements: function(rejInfo) {
		var cells = this.noteCells;
		if(rejInfo.algo) {
			for(var loop = 0; loop <  rejInfo.algo.length; loop++) {
				if(rejInfo.algo[loop] === null) cells[loop].textContent = '\u2714';
				else {
					cells[loop].textContent = "";
					for(var inner = 0; inner < rejInfo.algo[loop].length; inner++) {
						if(inner) cells[loop].textContent += ", ";
						cells[loop].textContent += rejInfo.algo[loop][inner];
					}
				}
			}
		}
	},
	
	updateConfigInfo: function(cinfo) {
		var tbody = document.getElementById("configInfo");
		while(tbody.childNodes.lastChild) tbody.childNodes.removeChild(tbody.childNodes.lastChild);
		for(var conf = 0; conf < cinfo.length; conf++) {
			server.config[conf] = {};
			
			var row = document.createElement("tr");			
			var devices = window.server.getDevicesUsingConfig(conf);
			staticCell(row, "[" + conf + "]", devices.length);
			staticCell(row, cinfo[conf].hashCount, devices.length);
			var mem = staticCell(row, Math.ceil(totalMU(cinfo[conf].resources) / 1024.0) + " KiB", devices.length);
			var report = makeMemoryReport(conf, cinfo[conf].resources);
			server.config[conf].memReport = report;
			server.config[conf].hashCount = cinfo[conf].hashCount;
			mem.appendChild(document.createElement("br"));
			mem.appendChild(makeReportButton(conf, report, "Details"));
			document.getElementById("miningStatus").appendChild(report);
			
			for(var d = 0; d < devices.length; d++) {
				if(d) row = document.createElement("tr");
				var cell = document.createElement("td");
				cell.textContent = "" + devices[d].linearIndex + ", ";
				var devColor = this.imgFromCanvas(devices[d].visPattern);
				compatible.modClass(devColor, "devicePattern", "add");
				cell.appendChild(devColor);
				row.appendChild(cell);
				tbody.appendChild(row);
			}
			if(devices.length == 0) {
				var whoops = document.createElement("td");
				whoops.colSpan = 4;
				whoops.textContent = "Configuration is unused.";
				row.appendChild(whoops);
				tbody.appendChild(row);
			}
		}
		
		function makeCell(tag, html) {
			var ret = document.createElement(tag);
			ret.innerHTML = html;
			return ret;
		}
		function staticCell(container, html, numDevices) {
			var cell = makeCell("td", html);
			cell.rowSpan = Math.max(numDevices, 1);
			container.appendChild(cell);
			return cell;
		}
		// is reduce supported? that would be nice to try
		function totalMU(values) {
			if(values === undefined || values.length == 0) return 0;
			var total = 0;
			for(var loop = 0; loop < values.length; loop++) total += values[loop].footprint;
			return total;
		}
		function makeReportButton(index, div, desc) {
			var btn = document.createElement("button");
			btn.textContent = desc;
			compatible.setEventCallback(btn, "click", function() {
				if(div.style.display === "none") div.style.display = "block";
				else div.style.display = "none";
			});
			return btn;
		}
		function makeMemoryReport(index, reslist) {
			var div = document.createElement("div");
			compatible.modClass(div, "memoryReport", "add");
			div.style.display = "none";
			div.style.zIndex = index * 10 + 50;
			div.innerHTML = "<h3>Memory usage estimation</h3>Due to driver policies, this is a <em>lower bound</em>.";			
			var table = document.createElement("table");
			var section = document.createElement("thead");
			var row = document.createElement("tr");
			row.appendChild(makeCell("th", "Address space"));
			row.appendChild(makeCell("th", "Used as"));
			row.appendChild(makeCell("th", "Description"));
			row.appendChild(makeCell("th", "Bytes used"));
			section.appendChild(row);
			table.appendChild(section);
			section = document.createElement("tbody");			
			for(var loop = 0; loop < reslist.length; loop++) {
				row = document.createElement("tr");
				row.appendChild(makeCell("td", reslist[loop].space));
				row.appendChild(makeCell("td", concat(reslist[loop].accessType)));
				row.appendChild(makeCell("td", reslist[loop].presentation));
				row.appendChild(makeCell("td", reslist[loop].footprint));
				section.appendChild(row);
			}
			table.appendChild(section);			
			div.appendChild(table);
			div.appendChild(makeReportButton(index, div, "Close"));
			return div;
			
			function concat(arr) {
				var ret = "";
				for(var s = 0; s < arr.length; s++) {
					if(s) ret += ", ";
					ret += arr[s];
				}
				return ret;
			}
		}
	},
	
	// To be called when first share has been found to update Avg window.
	refreshPerfHeaders : function(obj) {
		if(obj && obj.shortWindow) document.getElementById("shortWindow").textContent = obj.shortWindow;
		if(obj && obj.longWindow) document.getElementById("longWindow").textContent = obj.longWindow;
	},
	
	refreshDevicePerf : function() {
		var niceHR;
		var names = ["min", "max", "lavg", "savg", "last"];
		for(var loop = 0; loop < server.hw.linearDevice.length; loop++) { // if not hashrate mode, just ignore
			var check = server.hw.linearDevice[loop];
			if(check.configIndex === undefined) continue;
			
			var slowest = check.lastPerf[names[0]];
			for(var scan = 1; scan < names.legth; scan++) slowest = Math.max(slowest, check.lastPerf[names[scan]]);
			var fit = presentation.niceHashrate(1000 / slowest * server.config[check.linearIndex].hashCount);
			if(!niceHR || fit.divisor < niceHR.divisor) niceHR = fit;
		}
		
		for(var d = 0; d < server.hw.linearDevice.length; d++) {
			var device = server.hw.linearDevice[d];
			var cells = this.hashTimeCells[device.linearIndex];
			if(!cells) continue; // not built so not used!
			var hashCount = server.config[device.configIndex].hashCount;
			for(var loop = 0; loop < names.length; loop++) {
				var t = device.lastPerf? device.lastPerf[names[loop]] : undefined;
				var dst = cells[names[loop]];
				if(t === undefined) dst.textContent = "...";
				else if(this.perfMode === "itime") dst.textContent = t;
				else {
					var ips = 1000 / t;
					if(loop < 2) dst = cells[names[(loop + 1) % 2]]; // min and max must be swapped
					dst.textContent = Math.floor(hashCount * ips / niceHR.divisor);
				}
			}
		}
		return niceHR;
	},
	
	// Policy to decide how to visualize an hashrate value. Returns an object prefix and divisor.
	niceHashrate: function(value) {
		var ret = {};
		var prefix = [ "H", "K", "M", "G", "T", "P", "E", "Z", "Y" ];
		var divisor = 1;
		var loop;
		for(loop = 0; loop < prefix.length && divisor < Math.floor(value / divisor) * divisor; loop++)
			divisor *= 1000;
		divisor /= 1000;
		loop--;
		ret.divisor = divisor;
		ret.prefix = prefix[loop];
		return ret;
	},
	
	refreshDeviceShareStats: function(obj) {
		for(var loop = 0; loop < obj.linearIndex.length; loop++) {
			if(presentation.noncesCells[loop] === null) continue;
			var names = ["good", "bad", "stale"];
			var target = presentation.noncesCells[loop];
			for(var all = 0; all < names.length; all++) {
				target[names[all]].textContent = obj[names[all]][loop];
			}
		}
	},
	
	imgFromCanvas: function(canvas) {
		var img = document.createElement("img");
		img.src = canvas.toDataURL();
		return img;
	},
	
	hue: function(numColors, mode, seed) {
		var distance = 360.0 / (numColors + 1);
		var ret = [];
		ret.length = numColors;
		for(var loop = 0, h = distance; loop < numColors; loop++) {
			ret[loop] = h;
			h += distance;
		}
		if(mode === "pingpong") {
			var a = 1, b = numColors - 1;
			while(a < b) {
				var t = ret[a];
				ret[a] = ret[b];
				ret[b] = t;
				a += 2;
				b -= 2;
			}
		}
		else if(mode === "unpredictable" || (mode === "random" && seed === undefined)) {
			for(var loop = 0; loop < numColors; loop++) {
				var r = Math.floor(Math.random() *  (numColors - loop));
				var t = ret[loop];
				ret[loop] = ret[r];
				ret[r] = t;
			}
		}
		else if(mode === "random") {
			throw "Todo, repeatable random (Math.random is not!)";
		}
		return ret;
	},
	hsl: function(hue, saturation, luminance) {
		if(saturation === undefined) saturation = 100;
		else saturation = Math.Max(0, Math.min(100, saturation));
		if(luminance === undefined) luminance = 50;
		else luminance = Math.Max(0, Math.min(100, luminance));
		return "hsl(" + hue + ", " + saturation +"%, " + luminance + "%)";
	},
	glyphs: "\u25a0\u25b2\u25cf\u2660\u2663\u2665\u2666\u266a\u266b\u263c\u25ca\u25ac\u2605\u2668",
	patterns: function(size, count, options) {
		if(options === undefined) options = {};
		if(options.colors === undefined) {
			var temp = this.hue(count, "pingpong");
			options.colors = [];
			for(var loop = 0; loop < count; loop++) options.colors[loop] = this.hsl(temp[loop]);
		}
		var ret = [];
		for(var loop = 0; loop < count; loop++) {
			var canvas = document.createElement("canvas");
			canvas.width = canvas.height = size;
			var paint = canvas.getContext("2d");
			paint.save();
			paint.fillStyle = options.colors[loop];
			paint.fillRect(0, 0, size, size);
			ret.push(canvas);
			
			if(options.glyphs) {
				var g = loop % options.glyphs.length;
				paint.fillStyle = "black";
				paint.font = "" + size / 1.5 + "px sans-serif";
				paint.textBaseline = "middle";
				paint.textAlign = "center";
				paint.globalCompositeOperation = "destination-out";
				var draw = options.glyphs.substr(g, 1);
				paint.fillText(draw, size / 2, size / 2);	
				paint.globalCompositeOperation = "source-over";			
			}
			paint.restore();
		}
		return ret;
	}
};
