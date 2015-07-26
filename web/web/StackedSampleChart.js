"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

/* What is a "stacked sample chart"?
It is basically a stacked bar chart but the bars are 1-pixel wide.
Basically, you pass an array of samples. Those samples will get drawn.
A sample is: 

var sample = {
	x: ...,
	y: [ ... ]
};

It works this way: every time a draw() is called, the graph will find the min and max .x values across
the various samples. Those will be the extremes of the x axis.

The various values contained in the .y array will determine the extremes in the y axis. */
function StackedSampleChart(samples, canvas) {
	var ctx = canvas.getContext('2d');
	var self = this;
		
	function drawBars(x, ycoords) {
		var ybase = 0;
		for(var d = ycoords.length - 1; d + 1; d--) {
			var h = ycoords[d] * canvas.height;
			if(h === NaN) continue;
			ctx.fillStyle = self.areaFillStyle[d];
			ctx.fillRect(x, canvas.height - ybase, 1, -h);
			ybase += h;
		}
		
	}
	
	function getTop() {
		var biggest = 0;
		for(var loop = 0; loop < samples.length; loop++) {
			var sum = 0;
			for(var inner = 0; inner < samples[loop].y.length; inner++) sum += samples[loop].y[inner];
			if(sum > biggest) biggest = sum;
		}
		return Math.ceil(biggest * 1.05); // some margin
	}
	
	var top; // values used by last draw, to understand if we can incrementally upgrade.
	this.draw = function() {
		if(samples.length !== canvas.width) throw 'size mismatch, sample count is ' + samples.length + ' but only ' + canvas.width + ' pixels available';
		top = getTop();
		if(top === .0) return;
		
		var iscale = 1.0 / top;
		for(var loop = 0; loop < samples.length; loop++) {
			var y = [];
			y.length = samples[loop].y.length;
			for(var inc = 0; inc < y.length; inc++) y[inc] = samples[loop].y[inc] * iscale;
			drawBars(loop, y);
		}
	};
	this.clear = function() { ctx.clearRect(0, 0, canvas.width, canvas.height); }
	this.updated = function(count) {
		if(getTop() !== top) {
			this.clear();
			this.draw();
			return;
		}
		var iscale = 1.0 / top;
		var scroll = ctx.getImageData(count, 0, canvas.width - count, canvas.height);
		ctx.clearRect(canvas.width - count, 0, count, canvas.height);
		ctx.putImageData(scroll, 0, 0, 0, 0, scroll.width, scroll.height);
		for(var loop = samples.length - count; loop < samples.length; loop++) {
			var y = [];
			y.length = samples[loop].y.length;
			for(var inc = 0; inc < y.length; inc++) y[inc] = samples[loop].y[inc] * iscale;
			drawBars(loop, y);
		}
	}
}
StackedSampleChart.prototype = {
	areaFillStyle: null, // an array whose entry[i] is used to draw/fill, array of ImageData objects
	draw: function() { }, // draw the whole chart. Can get expensive...
	clear: function() { },
	updated: function(i) { } // the oldest i entries are out. Others are the same. i new entries, incremental update.
};
