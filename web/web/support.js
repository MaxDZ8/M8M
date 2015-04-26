"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */

var compatible = {
  setEventCallback : function(receiver, eventName, callback, capturing) {
    try {
      if(receiver.addEventListener) receiver.addEventListener(eventName, callback, capturing);
      else if(receiver.attachEvent) receiver.attachEvent     (eventName, callback);
      else alert("ERROR: I don't know how to define event listeners.");
    } catch(exc) { alert(exc); }
  },
  modClass : function(htmlElem, token, mode) {
	if(htmlElem instanceof Array && token instanceof Array) {
		for(var el = 0; el < htmlElem.length; el++) {
			for(var tag = 0; tag < token.length; tag++) this.modClass(htmlElem[el], token[tag], mode);
		}
	}
	else if(token instanceof Array) {
		for(var tag = 0; tag < token.length; tag++) this.modClass(htmlElem, token[tag], mode);
	}
	else if(htmlElem instanceof Array) {
		for(var el = 0; el < htmlElem.length; el++) this.modClass(htmlElem[el], token, mode);
	}
	else {
		if(htmlElem.classList && htmlElem.classList.length !== undefined && htmlElem.classList instanceof DOMTokenList) {
			htmlElem.classList[mode](token);
		}
		else throw "todo-modify class list by string .className";
	}
  },
  getElementById : function(node, id) {
	if(node.id === id) return node;
	var count = node.childNodes.length;
	for(var loop = 0; loop < count; loop++) {
		if(node.childNodes[loop]) {
			var find = this.getElementById(node.childNodes[loop], id);
			if(find) return find;
		}
	}
	return null;
  }
};
