"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

function CLPlatformDesc(serverIndex) {
	if(serverIndex === undefined) throw "All platforms must be mapped to a server internal structure.";
	this.index = serverIndex;
}
CLPlatformDesc.prototype = {
	profile: "unknown",
	version: "unknown",
	name: "unknown",
	vendor: "unknown",
	devices: [],
	fromMemory: function(object) { // what this does is to copy from a json object to an object having this base prototype
		if(!object.profile || !object.version || !object.name || !object.vendor) throw "Incomplete platform descriptor.";
		this.profile = object.profile;
		this.version = object.version;
		this.name = object.name;
		this.vendor = object.vendor;
		for(var i = 0; i < object.devices.length; i++) {
			var add = new CLDeviceDesc();
			add.fromMemory(object.devices[i]);
			this.devices.push(add);
		}
	}
};
