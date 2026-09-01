'use strict';

(function (global) {
  global.__DEV__ = true;

  function failingModuleFactory() {
    throw new Error('react-native-linux: intentional bundle failure');
  }

  console.log('react-native-linux: failing bundle evaluated');

  failingModuleFactory();
})(this);
