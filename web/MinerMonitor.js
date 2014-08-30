"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

function MinerMonitor(hostname, port, callbacks) {
	if(!callbacks) callbacks = { };
	var pendingReply = [];
	var socket = new WebSocket('ws://' + hostname + ':' + port + "/monitor", 'M8M-monitor');
	function wsERROR() {
		if(callbacks.fail) callbacks.fail("Websocket connection failed");
	};
	function wsReady() {
		if(callbacks.success) callbacks.success();
	}
	compatible.setEventCallback(socket, "error", wsERROR);
	compatible.setEventCallback(socket, "open", wsReady);
	compatible.setEventCallback(socket, "message", service);
	compatible.setEventCallback(socket, "close", function() { alert("socket close"); });
	
	var self = this;
	
	this.request = function(object, callback, streaming) {
		if(object.command === undefined) throw "Missing command name";
		/*! A request is a command generating a single reply.
		There are two types of requests. Requests generating persistent state are considered important
		and go through additional validation converting a json object to a specific type so instanceof can
		work as expected. Others generate temporary results so I can go easier on the assumptions.
		This however happens in the outer code and I don't really care about it. */
		var message = JSON.stringify(object);
		socket.send(message);
		var sent = Date.now();
		if(streaming) {
			var add = {};
			add.command = object.command.substr(0, object.command.length - 1) + "!";
			add.callback = callback;
			if(self.streaming === undefined) self.streaming = [];
			self.streaming.push(add);
		} // streaming commands have something MORE, but they also generate a standard response (eventually error)
		pendingReply.push(function(serverReply) {
			var received = Date.now();
			if(callbacks.pingTimeFunc) callbacks.pingTimeFunc((received - sent) / 1000);
			callback(serverReply);
		});
	}
	
	function service(message) {
		if(message.data.substr(0, 2) == "!!") {
			if(pendingReply.length) {
				for(var back = 0; back < pendingReply.length - 1; back++) pendingReply[back] = pendingReply[back + 1];
				pendingReply.pop();
			}
			return;
		}
		var object = JSON.parse(message.data);
		if(object.pushing !== undefined) {
			var len = self.streaming === undefined? 0 : self.streaming.length;
			for(var test = 0; test < len; test++) {
				var match = self.streaming[test];
				if(match.command.substr(0, object.pushing.length) == object.pushing) {
					if(match.closing !== undefined) {
						if(match.endStream) match.endStream();
						for(var mv = test; mv + 1 < len; mv++) {
							self.streaming[mv] = self.streaming[mv + 1];
						}
						return;
					}
					match.callback(object);					
				}
			}
			return; // unmatched pushes are ignored.
		}
		if(pendingReply.length == 0) return;
		// todo: filter by message.target might be a good idea? or maybe by origin?
		var callback = pendingReply[0];
		for(var back = 0; back < pendingReply.length - 1; back++) pendingReply[back] = pendingReply[back + 1];
		pendingReply.pop();
		callback(object); // this is more resistant to exceptions.
	}
	this.socket = socket;
}
MinerMonitor.prototype = {
	request: undefined,
	streaming: undefined,
	requestSimple: function(simpleCMDNoArgs, callback) {
		this.request({ command: simpleCMDNoArgs}, callback);
	},
	requestStream: function(object, callback) {
		object.push = true;
		this.request(object, callback, true);
	}
};

	
function request_system(minerMonitor, onreply) {
	minerMonitor.request({ command: "system?" }, mangle);
	function mangle(object, pingTime) {
		if(object.platforms === undefined || object.platforms.length === undefined) alert("Invalid platform reply from server!>>" + JSON.stringify(object));
		var sp = [];
		for(var i = 0; i < object.platforms.length; i++) { // those objects look like CLPlatformDescs, but they really are not!
			var add = new CLPlatformDesc(i);
			add.fromMemory(object.platforms[i]);
			sp.push(add); // now it is the same thing so my functions are supposed to be there and nothing will go wrong.
		}
		object.platforms = sp;
		onreply(object, pingTime);
	}
}
