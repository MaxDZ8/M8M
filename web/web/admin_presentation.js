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
	showReplyTime: function(pings) {
		document.getElementById("ping").textContent = Math.ceil(pings * 1000);
	},
};
