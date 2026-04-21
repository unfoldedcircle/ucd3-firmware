#!/usr/bin/env node
// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: Apache-2.0
//
// Dock WebSocket connection test.
// Verifies that oldest connection automatically closes for new connections
// after connection limit is reached.
//
// Requires Node 22+ or Deno 2
//
// Recommended to use with Deno:
// - Node will keep connections open if no WS close frame was received and won't emit the close event,
//  for example if the connection was force closed by LRU to re-use the oldest socket.
// - Deno is more aggressive and will close the WS client if the TCP socket was closed by the server.
//
// Usage:
// node ws_count.js <DOCK_IP>
// 
// Example:
// node ws_count.js /192.168.1.234
//

const TARGET_PORT = 80;
const TARGET_PATH = '/ws';
const CONNECTION_COUNT = 100;
const CONNECT_DELAY = 100;
const HOLD_MS = 10000;

const opened = new Map();
const pending = [];

if (process.argv.length !== 3) {
    console.error('Expected exactly one argument with the dock IP address!');
    process.exit(1);
}

console.log(`Opening ${CONNECTION_COUNT} WebSocket clients with ${CONNECT_DELAY}ms delay`);

for (let i = 0; i < CONNECTION_COUNT; i++) {
  pending.push(new Promise((resolve) => {
    let settled = false;
    const ws = new WebSocket(`ws://${process.argv[2]}:${TARGET_PORT}${TARGET_PATH}`);

    const finish = () => {
      if (!settled) {
        settled = true;
        resolve();
      }
    };

    ws.addEventListener('open', () => {
      opened.set(i, ws);
      console.log(`Connected ${i + 1}`);
      finish();
    }, { once: true });

    ws.addEventListener('close', (event) => {
      opened.delete(i);
      console.log(`Closed ${i + 1}: code=${event.code}, wasClean=${event.wasClean}, state=${ws.readyState}`);
      finish();
    }, { once: true });

    ws.addEventListener('error', (err) => {
      console.log(`Failed at ${i + 1}: ${err?.message || err}`);
      finish();
    }, { once: true });
  }));
  await new Promise((resolve) => setTimeout(resolve, CONNECT_DELAY));
}

await Promise.all(pending);

console.log(`Opened all WS client connections. Waiting for ${HOLD_MS} ms...`);

await new Promise((resolve) => setTimeout(resolve, HOLD_MS));

console.log(`After waiting there are ${opened.size} open clients: ${Array.from(opened.keys()).map(key => key + 1).join(',')}`)
for (const [id, ws] of opened) {
  console.log(`Closing WS client: ${id + 1}, state before=${ws.readyState}`);
  ws.close(1000, 'test complete');
}

console.log("Waiting for 2sec...");
await new Promise((resolve) => setTimeout(resolve, 2000));

console.log(`${opened.size} open clients: ${Array.from(opened.keys()).map(key => key + 1).join(',')}`)

// All WS connections **should** be closed by now. But Node.js will always wait for the close frame,
// and any force closed connections with active LRU remain open!
// Deno will emit the close event if the socket was closed without a close frame!
if (opened.size > 0) {
  // the dock closes unauthenticated WS connections after 30s
  console.log("Waiting 20s for dock closing remaining unauthenticated clients, then force exiting")
  setTimeout(() => {
    console.log(`${opened.size} open clients: ${Array.from(opened.keys()).map(key => key + 1).join(',')}`)
    console.log("Forcing process exit for test completion.");
    process.exit(0);
  }, 20000);
}
