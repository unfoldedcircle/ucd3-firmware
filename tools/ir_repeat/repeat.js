// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: Apache-2.0
//
// IR repeat test app. Send an IR command, followed by repeat commands for a given duration
//
// Usage:
// node repeat.js ws://UCD3-xxxxxx.local:946/ CODE REPEAT [DURATION] [DELAY]
//   default duration: 2000ms
//   default delay:     200ms
// Example:
// node repeat.js ws://172.16.16.123/ws "17;0x2A4C0A8A0282;48;3" 3
//
const WebSocket = require("ws");
require('log-timestamp');

const msg = {
    "type": "dock",
    "id": 0,
    "command": "ir_send",
    "code": "17;0x2A4C0A8A0282;48;3",
    "format": "hex",
    "repeat": 3,
    "int_side": true,
    "int_top": false,
    "ext1": true,
    "ext2": true
};

const stopMsg = {
    "type": "dock",
    "id": 123,
    "command": "ir_stop"
};

let msgId = 0;
// delay between repeat commands
let delayMs = 200;
// total duration of repeat
let transmitDurationMs = 2000;

if (process.argv.length < 5) {
    console.error('Usage: node repeat.js URL CODE REPEAT [DURATION] [DELAY]');
    console.error('  default duration: %dms', transmitDurationMs);
    console.error('  default delay   : %dms', delayMs);
    process.exit(1);
}

msg.code = process.argv[3];
if (msg.code.startsWith('sendir')) {
    msg.format = 'gc';
} else if (msg.code.includes(';')) {
    msg.format = 'hex';
} else {
    msg.format = 'pronto';
}
const repeat = parseInt(process.argv[4]);
if (isNaN(repeat)) {
    console.error('Invalid repeat parameter');
    process.exit(1);
}
msg.repeat = repeat;

if (process.argv.length > 5) {
    transmitDurationMs = parseInt(process.argv[5]);
    if (isNaN(transmitDurationMs)) {
        console.error('Invalid duration parameter');
        process.exit(1);
    }
}
if (process.argv.length > 6) {
    delayMs = parseInt(process.argv[6]);
    if (isNaN(delayMs)) {
        console.error('Invalid delay parameter');
        process.exit(1);
    }
}

console.log('Using dock command: %s', JSON.stringify(msg));

const ws = new WebSocket(process.argv[2]);

ws.on('open', function open() {
    console.log('Connected! Sending authentication');

    ws.send('{"type": "auth", "token": "0000"}');

    for (let i = delayMs; i < transmitDurationMs; i += delayMs) {
        setTimeout(send_ir, i);
    }

    setTimeout(send_ir_stop, transmitDurationMs + delayMs);

    setTimeout(function stop() {
        ws.close();
    }, transmitDurationMs + 400);
});

ws.on('message', function message(data) {
    console.log('received: %s', data);
});

ws.on('error', function error(msg) {
    console.error(msg);
    ws.close()
});

ws.on('close', function close() {
    console.log('Closed');
});

function send_ir() {
    msgId++;
    msg.id = msgId;
    console.log('Sending IR: %d', msgId);
    ws.send(JSON.stringify(msg));
}

function send_ir_stop() {
    msgId++;
    stopMsg.id = msgId;
    console.log('Sending Stop: %d', msgId);
    ws.send(JSON.stringify(stopMsg));
}
