'use strict';

const safeRequire = require('./common').safeRequire;
const serializerFactory = require('./serializer-factory');

const jsrs = {};
module.exports = jsrs;

// Change to true if you want to use the native C++ version of JSRS serializer.
// Currently the JavaScript version is quite faster (contrary to the parser,
// C++ version of which is a lot faster than the JavaScript one) though it is
// one of our priorities to optimize it.
const USE_NATIVE_SERIALIZER = false;

const jstpNative =
  safeRequire('../build/Release/jstp') ||
  safeRequire('../build/Debug/jstp');

if (jstpNative) {
  Object.assign(jsrs, jstpNative);
  if (!USE_NATIVE_SERIALIZER) {
    jsrs.stringify = serializerFactory.createSerializer();
  }
} else {
  console.warn(
    'JSTP native addon is not built. ' +
    'Run `npm install` in order to build it, otherwise you will get ' +
    'poor server performance under load.'
  );
  module.exports = require('./record-serialization-fallback');
}
