"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

function CLDeviceDesc() {
}
CLDeviceDesc.prototype = {
	vendorID: "unknown",
	type: "not probed",
	chip: "not probed",
	arch: "unrecognized",
	nclock: "unknown",
	clusters: "not probed",
	memory: "unknown",
	cache: "unknown",
	lds: "unknown",
	ldsType: "unknown",
	
	fromMemory: function(object) { // what this does is to copy from a json object to an object having this base prototype
		this.chip = object.chip;
		this.clusters = object.clusters;
		this.nclock = object.coreClock;
		this.memory = object.globalMemBytes;
		this.cache = object.globalMemCacheBytes;
		this.lds = object.ldsBytes;
		this.arch = object.arch;
		this.type = object.type;
		this.ldsType = object.ldsType;
	}
};
