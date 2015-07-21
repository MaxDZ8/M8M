"use strict"
/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
 
/* This object represents the (potentially remote) mining application we're talking to.
It is meant to map to miner state or config, it contains both static and dynamic data. */
window.miner = {
    compiled: null, // string obtained from "version" command
    msg: null, // string obtained from "version" command
   
    programStart: null, // date objects from 'uptime'
    hashingStart: null,
    firstNonce: null,
     
    algos: {},
    /*!< This is populated using the "algos" command according to protocol v4 (but not the same thing). Static data.
    The config tool has its own set of algorithms and implementations. The intersection
    of the two sets can be configured. */
   
    api: null, // taken from "systemInfo" reply.API, a string
    platforms: [], // 'systemInfo' reply.platforms, with device entries augmented by a .linearIndex and .hashCount (only if used)
};
