'use strict';

(function (global) {
  global.__BUNDLE_START_TIME__ = 0;
  global.__DEV__ = true;

  console.log('react-native-linux: bundle evaluated');

  Promise.resolve().then(function () {
    console.log('react-native-linux: microtask drained');
  });

  setTimeout(function () {
    console.log('react-native-linux: timer fired');

    setTimeout(function () {
      console.log('react-native-linux: nested timer fired');
      console.log('react-native-linux: bundle done');
    }, 5);
  }, 10);
})(this);
