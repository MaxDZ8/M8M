"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

function MinerMonitor(hostname, port, resource, callbacks) {
	if(callbacks) this.callbacks = callbacks;	
	var self = this;
	var pendingReply = [];
	var socket = new WebSocket('ws://' + hostname + ':' + port + "/" + resource, "M8M-" + resource);
	function wsERROR() {
		if(self.callbacks.fail) self.callbacks.fail("Websocket connection failed");
	};
	function wsReady() {
		if(self.callbacks.success) self.callbacks.success();
	}
	function wsClose() {
		if(self.callbacks.close) self.callbacks.close();
	}
	compatible.setEventCallback(socket, "error", wsERROR);
	compatible.setEventCallback(socket, "open", wsReady);
	compatible.setEventCallback(socket, "message", service);
	compatible.setEventCallback(socket, "close", wsClose);
	
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
			add.command = object.command;
			add.callback = callback;
			if(self.streaming === undefined) self.streaming = [];
			self.streaming.push(add);
		} // streaming commands have something MORE, but they also generate a standard response (eventually error)
		pendingReply.push(function(serverReply) {
			var received = Date.now();
			if(self.callbacks.pingTimeFunc) self.callbacks.pingTimeFunc((received - sent) / 1000);
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
		// starting from protocol v1.1 replies can be null, so I must check it first
		if(object && object.pushing !== undefined) {
			var len = self.streaming === undefined? 0 : self.streaming.length;
			for(var test = 0; test < len; test++) {
				var match = self.streaming[test];
				if(match.command == object.pushing) {
					if(match.closing !== undefined) {
						if(match.endStream) match.endStream();
						for(var mv = test; mv + 1 < len; mv++) {
							self.streaming[mv] = self.streaming[mv + 1];
						}
						return;
					}
					/*! todo: .stream sub-stream selector. */
					match.callback(object.payload);					
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
	callbacks: {},
	requestSimple: function(simpleCMDNoArgs, callback) {
		this.request({ command: simpleCMDNoArgs}, callback);
	},
	requestStream: function(object, callback) {
		object.push = true;
		this.request(object, callback, true);
	}
};
