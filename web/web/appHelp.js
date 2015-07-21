"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
 /* Various snippets of code used in various places. */
 window.appHelp = (function() {
    var zIndex = 5;
    function showHideBTNFunc(btn, div, click) {
        if(div.style.display === "none") {
            div.style.display = "block"; // get sized bro!
            if(div.style.top === "") {
                div.style.top = (click.screenY - div.clientHeight) + "px";
                div.style.left = "50px";
            }
        }
        else div.style.display = "none";
    };
    function extractPXMeasure(str) { return str.substr(0, str.length - 2); };
    
        
    // Policy to decide how to visualize an hashrate value. Returns an object prefix and divisor.
    function isoDivisors(accurate, prefix, increment) {
        var ret = {};
        var divisor = 1;
        var diff, loop = 0;
        do {
            var value = Math.floor(accurate / divisor);
            diff = Math.abs(accurate - value);
            divisor *= increment;
            loop++;
            
        } while(diff < accurate * .01);
        divisor /= increment;
        loop--;
        ret.divisor = divisor;
        ret.prefix = prefix[loop];
        return ret;
    }
	
	
    function hue(numColors, mode, seed) {
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
    }
    function hsl(hue, saturation, luminance) {
        if(saturation === undefined) saturation = 100;
        else saturation = Math.Max(0, Math.min(100, saturation));
        if(luminance === undefined) luminance = 50;
        else luminance = Math.Max(0, Math.min(100, luminance));
        return "hsl(" + hue + ", " + saturation +"%, " + luminance + "%)";
    }

    
    return {
        // Create a button with the specified description which on click will show/hide the given div.
        makeDetailShowHideButton: function(div, desc) {
            var btn = document.createElement("button");
            btn.innerHTML = desc;
            compatible.setEventCallback(btn, "click", function(click) {
                showHideBTNFunc(btn, div, click);
            });
            return btn;
        },
        makeFloatingDetailsDiv: function() {
            var div = document.createElement("div");
            compatible.modClass(div, "detailsBox", "add");
            div.style.display = "none";
            div.style.zIndex = zIndex++;
            
            // Also add drag functionality, sort of.
            /*! Ugly shit about browsers. Gecko has it right. It has a 'buttons' property being a bitmask of buttons pushed.
            It seems recent IE also has it. CHROME NOT.
            Ok, so the 'button' property, signal the index of the pressed button. Besides it can only signal a single button state, indices
            start at 0 so there's no way to know if no button is pressed! WTF W3C !!!
            No problem. There's .which. It's 0 if not pressed or 1+button. Firefox always sets this to 1 !!! 
            So here we go for the ugly shit that follows: register mouseup and mousedown so we know what's down and what's not. */
            var moveState = {};
            compatible.setEventCallback(div, "mousedown", function(event) {
                if(event.target !== div) return;
                if(event.button === 0) moveState.pushPoint = [ event.clientX, event.clientY ];
            }, false);
            compatible.setEventCallback(div, "mouseup", function(event) {
                // don't miss up events in contained rects (+ or -), will leave dragging state off... fairly odd to see
                if(event.target !== div && div.contains(event.target) === false) return;
                if(event.button === 0) moveState.pushPoint = undefined;
            }, false);
            compatible.setEventCallback(div, "mousemove", function(event) {
                if(!moveState.pushPoint) return;
                if(event.target !== div) return;
                var dx = event.clientX - moveState.pushPoint[0];
                var dy = event.clientY - moveState.pushPoint[1];
                var x = 1 * extractPXMeasure(div.style.left);
                var y = 1 * extractPXMeasure(div.style.top );
                div.style.left = (x + dx) + "px";
                div.style.top  = (y + dy) + "px";
                moveState.pushPoint = [ event.clientX, event.clientY ];
            }, false);
            // Is the above sufficient? Of course not! In theory if the dragging goes out of the rectangle (which is likely, as we
            // move the rectangle AFTER the mouse moved) then we have to cancel dragging operation:
            compatible.setEventCallback(div, "mouseleave", function(event) { 
                if(event.target !== div) return;
                moveState.pushPoint = undefined;
            }, false);
            // ^ this makes dragging slightly inconvenient: one might want to allow some pixels of tolerance and keep memory of the
            // dragging operation. That's not so easy as we might click on the hide button after that.
            // Mouse capture would probably be the right tool for the job here but it just doesn't seem worth the effort!
            return div;
        },
        makeCell: function(tag, html) {
            var ret = document.createElement(tag);
            if(html !== null) ret.innerHTML = html;
            return ret;
        },
        appendCell: function(container, html, rowSpan) {
            var cell = window.appHelp.makeCell("td", html);
            if(rowSpan !== undefined) cell.rowSpan = Math.max(rowSpan, 1); // so third parameter can be optional
            container.appendChild(cell);
            return cell;
        },
        readableHHHHMMSS: function(elapsedms) {
            var initial = elapsedms;
            var elapsed = initial;
            elapsed = Math.floor(elapsed / 1000);
            var count = Math.floor(elapsed / (60 * 60));
            var readable = "";
            if(count) {
                readable += count;
                elapsed -= count * 60 * 60;
            }
            count = Math.floor(elapsed / 60);
            if(count || readable.length) {
                if(readable.length) {
                    readable += ":";
                    if(elapsed < 10) readable += "0";
                }
                readable += "" + count;
                elapsed -= count * 60;
            }
            if(elapsed || readable.length) {
                if(readable.length) {
                    readable += ":";
                    if(elapsed < 10) readable += "0";
                }
                readable += "" + elapsed;
            }
            else if(readable.length === 0) readable = "0";
            return readable;
        },
        makeLegendCanvas: function(pattern, sizepx) {
            var canvas = document.createElement("canvas");
            canvas.width = canvas.height = sizepx;
            var paint = canvas.getContext("2d");
            //paint.save();
			paint.fillStyle = pattern;
            paint.fillRect(0, 0, sizepx, sizepx);
            //paint.restore();
            canvas.className = 'devicePattern';
            return canvas;
        },
        makeMemoryReport: function(devLinearIndex, details, reslist) {
            var div = window.appHelp.makeFloatingDetailsDiv();
            div.innerHTML = '<h3>Memory usage estimation</h3><strong>For device ' + devLinearIndex + '</strong><br>'
                          + details + '<br>'
                          + 'Due to driver policies, this is a <em>lower bound</em>.';
            var table = document.createElement("table");
            var section = document.createElement("thead");
            var row = document.createElement("tr");
            row.appendChild(window.appHelp.makeCell("th", "Address space"));
            row.appendChild(window.appHelp.makeCell("th", "Used as"));
            row.appendChild(window.appHelp.makeCell("th", "Bytes used"));
            row.appendChild(window.appHelp.makeCell("th", "Notes"));
            section.appendChild(row);
            table.appendChild(section);
            section = document.createElement("tbody");
            for(var loop = 0; loop < reslist.length; loop++) {
                row = document.createElement("tr");
                row.appendChild(window.appHelp.makeCell("td", reslist[loop].space));
                row.appendChild(window.appHelp.makeCell("td", reslist[loop].presentation));
                row.appendChild(window.appHelp.makeCell("td", reslist[loop].footprint));
                row.appendChild(window.appHelp.makeCell("td", concat(reslist[loop].notes)));
                section.appendChild(row);
            }
            table.appendChild(section);            
            div.appendChild(table);
            div.appendChild(window.appHelp.makeDetailShowHideButton(div, "Close"));
            return div;
            
            function concat(arr) {
                var ret = "";
                for(var s = 0; s < arr.length; s++) {
                    if(s) ret += ", ";
                    ret += arr[s];
                }
                return ret;
            }
        },
        base10Divisor: function(accurate) { return isoDivisors(accurate, [ "", "K", "M", "G", "T", "P", "E", "Z", "Y" ], 1000); },
        base2Divisor: function(accurate) { return isoDivisors(accurate, [ "", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi", "Yi" ], 1024); },
        isoDivisorString: function(accurate, reductor, func) {
            var div = reductor(accurate);
            if(func === undefined) func = 'floor';
            return Math[func](accurate / div.divisor) + ' ' + div.prefix;
        },
		
		glyphs: function() {
			var original = "\u25a0\u25b2\u25cf\u2660\u2663\u2665\u2666\u266a\u266b\u263c\u25ca\u25ac\u2605\u2668";
			return original.substr(0);
		},
	
        patterns: function(size, count, options) {
            if(options === undefined) options = {};
            if(options.colors === undefined) {
                var temp = hue(count, "pingpong");
                options.colors = [];
                for(var loop = 0; loop < count; loop++) options.colors[loop] = hsl(temp[loop]);
            }
            var ret = [];
			var canvas = document.createElement("canvas");
			canvas.width = canvas.height = size;
			var paint = canvas.getContext("2d");
            for(var loop = 0; loop < count; loop++) {
                paint.save();
                paint.fillStyle = options.colors[loop];
                paint.fillRect(0, 0, size, size);
                
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
                ret.push(paint.getImageData(0, 0, size, size));
            }
            return ret;
        }
    };
 })();
 