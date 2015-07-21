"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

 /* Some bookkeeping of client app-state. */
window.monitorState = (function() {
    var holding; // <canvas> objects to be used as patterns, because CanvasRenderingContext2D.createPattern cannot mangle ImageData for some reason, and the relative pattern generated
	var PATTERN_SIZE_PX = 20;
	
	function genPatterns() {
		var numColors = window.monitorState.usedDevices.length;
		if(numColors === 0) return;
		var patterns;
		if(numColors <= 10) patterns = window.appHelp.patterns(PATTERN_SIZE_PX, numColors, undefined);
		else {
			patterns = window.appHelp.patterns(PATTERN_SIZE_PX, numColors, { glyphs: window.appHelp.glyphs() });
			if(window.monitorState.usedDevices.length > 20) alert('How did you manage to pack more than 20 devices in a system?\nThe graph is screwed.');
		}
		holding = [];
		for(var gen = 0; gen < patterns.length; gen++) {
			var pair = {};
			pair.canvas = document.createElement('canvas');
			pair.canvas.width = pair.canvas.height = PATTERN_SIZE_PX;
			var ctx = pair.canvas.getContext('2d');
			ctx.putImageData(patterns[gen], 0, 0);
			pair.pattern = ctx.createPattern(pair.canvas, 'repeat');
			holding.push(pair);
		}
	}
	
    return {
        usedDevices: [], // array of objects, see reply callback to 'configInfo'
		configuredPools: [], // array of objects populated by 'pools' callback
        getDevicePattern: function(devLinear) {
            if(!holding) genPatterns();
            for(var check in window.monitorState.usedDevices) {
                if(window.monitorState.usedDevices[check].device.linearIndex === devLinear) return holding[check].pattern;
            }
            // unreachable by construction
            return undefined;
        }
    };
})();
