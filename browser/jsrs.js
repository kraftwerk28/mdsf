'use strict';

(function() {

  var jsrs = {};

  if (module && module.exports) {
    module.exports = jsrs;
  } else {
    if (!window.api) window.api = {};
    if (!window.api.jstp) window.api.jstp = {};
    jsrs = window.api.jstp;
  }

  // Serializes a JavaScript value using the JSTP Record Serialization format
  // and returns a string representing it.
  //   object - an object to serialize
  //
  jsrs.stringify = function stringify(object) {
    var type;
    if (Array.isArray(object)) {
      type = 'array';
    } else if (object instanceof Date) {
      type = 'date';
    } else if (object === null) {
      type = 'null';
    } else {
      type = typeof(object);
    }

    var serializer = jsrs.stringify.types[type];
    if (serializer) {
      return serializer(object);
    }

    return '';
  };

  jsrs.stringify.types = {
    number: function(number) {
      return number + '';
    },

    boolean: function(bool) {
      return bool ? 'true' : 'false';
    },

    undefined: function() {
      return 'undefined';
    },

    null: function() {
      return 'null';
    },

    string: function(string) {
      var content = JSON.stringify(string).slice(1, -1);
      return '\'' + content.replace(/'/g, '\\\'') + '\'';
    },

    date: function(date) {
      return '\'' + date.toISOString() + '\'';
    },

    array: function(array) {
      return '[' + array.map(jsrs.stringify).join(',') + ']';
    },

    object: function(object) {
      var result = '{';
      var firstKey = true;

      for (var key in object) {
        if (!object.hasOwnProperty(key)) {
          continue;
        }

        var value = jsrs.stringify(object[key]);
        if (value === '' || value === 'undefined') {
          continue;
        }

        if (!/[a-zA-Z_]\w*/.test(key)) {
          key = jsrs.stringify.types.string(key);
        }

        if (firstKey) {
          firstKey = false;
        } else {
          result += ',';
        }

        result += key + ':' + value;
      }

      return result + '}';
    }
  };

  // Deserializes a string in the JSTP Record Serialization format into
  // a JavaScript value and returns it.
  //   string - a string to parse
  //
  jsrs.parse = function parse(string) {

  };

})();
