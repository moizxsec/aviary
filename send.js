#!/usr/bin/env node
'use strict';

// usage: node send.js "your letter" [--from name] [--bird phoenix]

const http = require('http');

const PORT = Number(process.env.AVIARY_PORT || 45874);
const argv = process.argv.slice(2);

let from = '';
let bird = 'phoenix';
const words = [];

for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--from') from = argv[++i] || '';
  else if (argv[i] === '--bird') bird = argv[++i] || 'phoenix';
  else words.push(argv[i]);
}

const text = words.join(' ').trim();
if (!text) {
  console.error('usage: npm run send -- "your letter" [--from name] [--bird phoenix]');
  process.exit(1);
}

const body = new URLSearchParams({ text, from, bird }).toString();

const req = http.request(
  { host: '127.0.0.1', port: PORT, path: '/send', method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded',
               'Content-Length': Buffer.byteLength(body) } },
  (res) => {
    res.resume();
    if (res.statusCode === 200) console.log('bird released.');
    else console.error('refused:', res.statusCode);
  }
);

req.on('error', () => {
  console.error(`no aviary on 127.0.0.1:${PORT} — run "npm start" first.`);
  process.exit(1);
});

req.end(body);
