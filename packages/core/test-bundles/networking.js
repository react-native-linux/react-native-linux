// The networking proof for issue #79: a request through upstream's C++ Networking TurboModule, which this platform
// supplies the libcurl HTTP client for, against a local server serving this directory.
//
// python3 -m http.server 8781 --bind 127.0.0.1 --directory packages/core/test-bundles &
// hello_react packages/core/test-bundles/networking.js
//
// There is no React and no XMLHttpRequest in a bare bundle, so this is what XMLHttpRequest does underneath: send
// through the module and listen on the device event emitter. The interval only keeps the host from reaching
// quiescence while the response is on the network, because a request is not a timer.

const turboModuleProxy = globalThis.__turboModuleProxy;
const networking =
  typeof turboModuleProxy === 'function' ? turboModuleProxy('Networking') : globalThis.nativeModuleProxy.Networking;
const requestId = 1;
let received = '';
const keepAlive = setInterval(() => {}, 10);

globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, payload) => {
    if (eventName === 'didReceiveNetworkResponse') {
      console.log(`networking: response ${payload[1]} for request ${payload[0]}`);
    } else if (eventName === 'didReceiveNetworkData') {
      received += payload[1];
    } else if (eventName === 'didCompleteNetworkResponse') {
      console.log(`networking: completed with ${received.length} bytes, error ${payload[1] ?? 'none'}`);
      console.log(`networking: body mentions the proof: ${received.includes('The networking proof for issue #79')}`);
      clearInterval(keepAlive);
    }
  },
};

networking.sendRequest('GET', 'http://127.0.0.1:8781/networking.js', requestId, [], {}, 'text', false, 5000, false);
