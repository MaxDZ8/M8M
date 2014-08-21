"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
 
function HRGraphPainter(target, deviceList) {
	var draw = target.getContext('2d');
	var secondsPerPixel = 1.0 / 8.0;
	var timeWindow = target.width * secondsPerPixel * 1000;
	var activeDevice = [];
	for(var loop = 0; loop < deviceList.length; loop++) {
		if(deviceList[loop].visPattern) activeDevice.push(deviceList[loop]);
	}
	var firstSample, firstReceive;
	var hashRate = []; // one entry for each column to fill.
	hashRate.length = target.width;
	for(var loop = 0; loop < hashRate.length; loop++) {
		hashRate[loop] = [];
		hashRate[loop].length = activeDevice.length;
		for(var init = 0; init < hashRate[loop].length; init++) hashRate[loop][init] = 0;
	}
	// We have no way to clear this anyway, nor need to do so as it never goes away!
	var refreshInterval = window.setInterval(drawGraph, 1000 / 2);
	var compositingCanvas = document.createElement("canvas");
	compositingCanvas.width = target.width;
	compositingCanvas.height = target.height;
	var compose = compositingCanvas.getContext('2d');
	
	// I called those .visPattern, but they really are not.
	var cpattern = [];
	for(var dev = 0; dev < deviceList.length; dev++) {
		if(!deviceList[dev].visPattern) continue;
		var realPattern = compose.createPattern(deviceList[dev].visPattern, "repeat");
		cpattern.push(realPattern);
	}
	
	var refreshHRCharts = true;
	this.appendData = function appendData(obj) {
		if(!firstSample) {
			firstReceive = new Date();
			firstSample = new Date(obj.sinceEpoch * 1000);
		}
		
		for(var loop = 0; loop < obj.device.length; loop++) {
			var when = new Date(obj.sinceEpoch * 1000 + obj.relative[loop] / 1000); // Java date in milliseconds
			var relativems = when - firstSample;
			if(relativems >= timeWindow) throw "TODO: flowing graph";
			var col = Math.floor((relativems / timeWindow) * target.width); // I expect the JITter to optimize here!
			var dev = obj.device[loop];
			var t = obj.iterationTime[loop];
			if(t === 0) hashRate[col][dev] = 0;
			else {
				t = Math.floor(t / 1000); // milliseconds please, they look nicer and more stable
				var iterations = 1000.0 / t;
				var hashCount = window.server.config[activeDevice[dev].configIndex].hashCount;
				if(hashRate[col][dev] === 0) hashRate[col][dev] = hashCount * iterations;
				else hashRate[col][dev] = Math.min(hashRate[col][dev], hashCount * iterations);
			}
		}
		refreshHRCharts = true;
	}
	
	var stacking = {
		minhr: undefined,
		maxhr: undefined
	};
	stacking.minhr = [];
	stacking.maxhr = [];
	stacking.minhr.length = stacking.maxhr.length = hashRate.length;
	var mask = [];
	mask.length = activeDevice.length;
	for(var init = 0; init < mask.length; init++) mask[init] = draw.createImageData(target.width, target.height);
	
	function drawGraph() {
		if(!firstSample) return; // we haven't received a single update so far, nothing to do!
		var hrTop = .0;
		if(refreshHRCharts) {
			for(var col = 0; col < hashRate.length; col++) stacking.minhr[col] = .0;
			for(var col = 0; col < hashRate.length; col++) stacking.maxhr[col] = hashRate[col][0];
			var devMax = [];
			for(var clear = 0; clear < activeDevice.length; clear++) devMax.push(.0);
			for(var col = 0; col < hashRate.length; col++) {
				for(var d = 0; d < activeDevice.length; d++) {
					devMax[d] = Math.max(devMax[d], hashRate[col][d]);
				}			
			}
			for(var add = 0; add < activeDevice.length; add++) hrTop += devMax[add];
			hrTop *= 1.05;
			
			var hrPerRow = hrTop / target.height;
			for(var dev = 0; dev < mask.length; dev++) {
				for(var pixel = 0; pixel < target.width * target.height; pixel++) {
					var off = pixel * 4; // for the time being, complete regeneration for each pass!
					mask[dev].data[off++] = 0; // not a lot of work anyway!
					mask[dev].data[off++] = 0;
					mask[dev].data[off++] = 0;
					mask[dev].data[off++] = 0;
				}
				for(var row = 0; row < target.height; row++) {
					// Small problem: graphs have origin lower left while rasters upper right...
					var fromBottom = target.height - (row + 1);
					var offset = fromBottom * target.width * 4;
					var midRow = row * hrPerRow + hrPerRow;
					scanLine(mask[dev].data, offset, target.width, midRow);
				}
				var swap = stacking.minhr;
				stacking.minhr = stacking.maxhr;
				stacking.maxhr = swap;
				for(var col = 0; col < stacking.maxhr; col++) {
					stacking.maxhr[col] = stacking.minhr[col] + hashRate[col][dev];
				}
			}
			refreshHRCharts = false;
		}
		
		// now the various masks are ready to go, we use them to select a part of pixels
		// in the compositing canvas and then fill those pixels with the pattern associated to device.
		draw.clearRect(0, 0, target.width, target.height);
		compose.save();
		compose.globalCompositeOperation = "source-in";
		for(var dev = 0; dev < activeDevice.length; dev++) {
			compose.clearRect(0, 0, compositingCanvas.width, compositingCanvas.height);
			compose.putImageData(mask[dev], 0, 0, 0, 0, compositingCanvas.width, compositingCanvas.height);
			compose.fillStyle = cpattern[dev];
			compose.fillRect(0, 0, compositingCanvas.width, compositingCanvas.height);
			draw.drawImage(compositingCanvas, 0, 0, target.width, target.height);
		}
		compose.restore();
		// Now, the server sends me this data in blocks... but I want to make it smooth so I lie to the user
		// and show a nice rolling graph.
		var valid =  new Date() - firstReceive;
		if(valid < timeWindow) { // if not, graph is full and we're scrolling
			valid /= 1000;
			valid /= secondsPerPixel;
			//draw.clearRect(valid, 0, target.width, target.height); // I take it easy
			draw.fillStyle = "rgba(0, 0, 0, .50)";
			draw.fillRect(valid, 0, target.width, target.height);
		}
	}
	
	
	function scanLine(pixel, start, numPixels, heightHR) {
		for(var scan = 0; scan < numPixels; scan++) {
			if(heightHR > stacking.maxhr[scan]) continue;
			if(heightHR < stacking.minhr[scan]) continue;
			var off = start + scan * 4;
			pixel[off++] = 255;
			pixel[off++] = 255;
			pixel[off++] = 255;
			pixel[off++] = 255;
		}
	}
}
HRGraphPainter.prototype = {
	appendData: null,
	topRate: 0 // highest hashrate, at uppermost pixel of the graph, sum of all top hashrates of all devices
};
