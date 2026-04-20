#!/usr/bin/env node
// SPDX-FileCopyrightText: Copyright (c) 2026 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: Apache-2.0
//
// Dock WebSocket connection test.
// Verifies that oldest connection automatically closes for new connections
// after connection limit is reached.
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
const HOLD_MS = 5000;

const opened = [];
const pending = [];

if (process.argv.length !== 3) {
    console.error('Expected exactly one argument with the dock IP address!');
    process.exit(1);
}

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
      opened.push(ws);
      console.log(`Connected ${i + 1}`);
      finish();
    }, { once: true });

    ws.addEventListener('close', () => {
      opened.pop(ws);
      console.log(`Closed ${i + 1}`);
      finish();
    }, { once: true });

    ws.addEventListener('error', (err) => {
      console.log(`Failed at ${i + 1}: ${err?.message || err}`);
      finish();
    }, { once: true });
  }));
  await new Promise((resolve) => setTimeout(resolve, 200));
}

await Promise.all(pending);

setTimeout(() => {
  for (const ws of opened) {
    ws.close(1000, 'test complete');
  }
}, HOLD_MS);
