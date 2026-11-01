#!/usr/bin/env node
/*
 * Pad a Sega Mega Drive ROM to a power-of-two size (>= 128 KiB) and compute the
 * standard 68k ROM checksum.
 *
 * This replaces SGDK's `sizebnd.jar` step (which is why the project's build avoids
 * requiring a JVM). Behaviour mirrors what that step does:
 *
 *   * Pad with zeros up to the next power of two, minimum 128 KiB.
 *   * Compute the checksum such that the 16-bit big-endian sum of the whole ROM
 *     (including the stored checksum word) equals 0x0000. The checksum field is
 *     at header offset 0x18E (bytes 0x18E..0x18F) and is excluded while summing.
 *
 * Usage:  node tools/pad_rom.js out/rom.bin
 *
 * NOTE: This environment has no Python interpreter (only a broken Windows Store
 * alias), so the tool is written in Node, which is reliably installed.
 */
'use strict';

const fs = require('fs');

const MIN_SIZE = 128 * 1024;
const CHECKSUM_OFFSET = 0x18E;

function nextPow2(n, minimum) {
  let t = 1;
  const min = Math.max(n, minimum);
  while (t < min) t <<= 1;
  return t;
}

function main() {
  const argv = process.argv.slice(2);
  if (argv.length < 1) {
    console.error('usage: node tools/pad_rom.js <path/to/rom.bin>');
    process.exit(2);
  }
  const p = argv[0];
  if (!fs.existsSync(p)) {
    console.error('error: ' + p + ' not found');
    process.exit(2);
  }

  let buf = fs.readFileSync(p);
  const originalSize = buf.length;
  const paddedSize = nextPow2(originalSize, MIN_SIZE);
  if (originalSize < paddedSize) {
    const padded = Buffer.alloc(paddedSize, 0);
    buf.copy(padded, 0);
    buf = padded;
  }

  // Compute checksum: zero the field first, sum all 16-bit BE words, then store
  // the two's complement so the total wrap-around sum is 0.
  buf.writeUInt16BE(0, CHECKSUM_OFFSET);
  let total = 0;
  for (let i = 0; i + 1 < buf.length; i += 2) {
    total += buf.readUInt16BE(i);
  }
  total &= 0xffff;
  const checksum = (0x10000 - total) & 0xffff;
  buf.writeUInt16BE(checksum, CHECKSUM_OFFSET);

  fs.writeFileSync(p, buf);
  console.log(
    'padded ' + originalSize + ' -> ' + buf.length + ' bytes ' +
    '(0x' + buf.length.toString(16) + '); checksum=0x' + checksum.toString(16).padStart(4, '0')
  );
  process.exit(0);
}

main();
