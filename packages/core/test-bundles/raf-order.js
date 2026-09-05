// The JavaScript-visible half of issue #263: requestAnimationFrame is FIFO, once per frame.
//
// hello_react --raf-trace packages/core/test-bundles/raf-order.js 3
//
// Every callback of one frame is invoked with one timestamp, so the frame a callback ran in is readable from
// JavaScript without the host telling it: the first timestamp seen is frame 1, the next distinct one frame 2.
// That is what makes "once per frame" an assertion a fixture can make rather than a claim about C++.
//
// Three rules, one trace. A, B and C are registered in that order before any frame runs. A registers D and
// cancels C. So: A and B run on frame 1 in registration order, C never runs because a callback of its own frame
// cancelled it, and D runs on frame 2 because a request made inside a callback belongs to the next frame — not to
// the frame that made it, which would be a spin, and not to a dispatch-thread heap ordered by deadline, which is
// what react-native#48005 reported as nondeterministic order.

'use strict';

(function (global) {
  global.__BUNDLE_START_TIME__ = 0;
  global.__DEV__ = true;

  var frameByTimestamp = new Map();
  var trace = [];

  function frameOf(timestamp) {
    if (!frameByTimestamp.has(timestamp)) {
      frameByTimestamp.set(timestamp, frameByTimestamp.size + 1);
    }

    return frameByTimestamp.get(timestamp);
  }

  function record(name) {
    return function (timestamp) {
      var entry = name + '@frame' + frameOf(timestamp);

      trace.push(entry);
      console.log('raf: ' + entry);
    };
  }

  var cancelledHandle = 0;

  console.log('raf: order start');

  requestAnimationFrame(function (timestamp) {
    record('A')(timestamp);
    cancelAnimationFrame(cancelledHandle);
    requestAnimationFrame(record('D'));
  });
  requestAnimationFrame(record('B'));
  cancelledHandle = requestAnimationFrame(record('C'));

  requestAnimationFrame(function () {
    // Frame 1 as well, and last, so this callback sees A and B but nothing of frame 2 yet. A queue that let D run
    // early, or that reordered A and B, fails here rather than in a diff of the lines above.
    console.log('raf: frame1 ' + trace.join(','));

    requestAnimationFrame(function () {
      requestAnimationFrame(function () {
        console.log('raf: trace ' + trace.join(','));
        console.log(
          trace.join(',') === 'A@frame1,B@frame1,D@frame2' ? 'raf: order ok' : 'raf: order wrong',
        );
      });
    });
  });
})(this);
