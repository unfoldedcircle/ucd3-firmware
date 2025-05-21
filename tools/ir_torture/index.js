// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: Apache-2.0
//
// Torture test app for continuously sending an IR code to a dock.
//
// Usage:
// node index.js ws://UCD3-xxxxxx.local:946/ [CODE]
// 
// Example:
// node index.js ws://172.16.16.123/ws "17;0x2A4C0A8A0282;48;1"
//
const WebSocket = require("ws");
require('log-timestamp');

let msg = {
    "type": "dock",
    "id": 123,
    "command": "ir_send",
    "code": "17;0x2A4C0A86028E;48;0",
    "int_side": true,
    "int_top": false,
    "ext1": true,
    "ext2": true,
    "format": "hex"
};

let count = 0;
// delay between ir_send commands
const delayMs = 100;

let interval = undefined;


if (process.argv.length === 2) {
    console.error('Expected at least one argument with the dock WS url!');
    process.exit(1);
}
if (process.argv.length === 4) {
    msg.code = process.argv[3];
    if (msg.code.startsWith('sendir')) {
        msg.format = 'gc';
    } else if (msg.code.includes(';')) {
        msg.format = 'hex';
    } else {
        msg.format = 'pronto';
    }
}

const ws = new WebSocket(process.argv[2]);

ws.on('error', function error(msg) {
    clearInterval(interval);
    console.error(msg);
    console.log('Number of sent messages: %d', count);
    ws.close()
});

ws.on('open', function open() {
    ws.send('{"type": "auth", "token": "0000"}');
    interval = setInterval(function send_ir() {
        count++;
        msg.id = count;
        ws.send(JSON.stringify(msg));
    }, delayMs);
});

ws.on('message', function message(data) {
    console.log('received: %s', data);
});

ws.on('close', function close() {
    clearInterval(interval);
    console.log('Closed. Number of sent messages: %d', count);
});
