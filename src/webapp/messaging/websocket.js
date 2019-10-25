(function () {
  // prevent double initialization
  if (window.audience !== undefined)
    return;

  // queues to handle init states properly
  var qin = [];
  var qout = [];

  // message handler
  var handlers = new Set();

  // connection to backend
  var ws = new WebSocket('ws' + location.origin.substr(4));

  // push routines
  function pushOutgoing() {
    while (ws.readyState == 1 && qout.length > 0) {
      try {
        ws.send(qout[0]);
        qout.shift();
      }
      catch (error) {
        console.error(error);
        break;
      }
    }
  }

  function pushIncoming() {
    while (handlers.size > 0 && qin.length > 0) {
      try {
        handlers.forEach(function (handler) {
          handler(qin[0]);
        });
        qin.shift();
      }
      catch (error) {
        console.error(error);
        break;
      }
    }
  }

  // websocket event handler
  ws.addEventListener('open', function (event) {
    pushOutgoing();
  });

  ws.addEventListener('message', function (event) {
    qin.push(event.data);
    pushIncoming();
  });

  // public interface
  window.audience = {};

  window.audience.postMessage = function (message) {
    // currently we support strings only
    if (typeof message != 'string')
      throw new Error('only string messages are supported');
    // put in queue and trigger send
    qout.push(message);
    pushOutgoing();
  };

  window.audience.onMessage = function (handler) {
    handlers.add(handler);
  };

  window.audience.offMessage = function (handler) {
    if (handler !== undefined) {
      handlers.delete(handler);
    }
    else {
      handlers.clear();
    }
  };
})();