/*
 * bmcart.js: reader of the .bm container for the web player (docs/BM-FORMAT.md).
 * It checks the header, the CRC-32 and the section table as src/bm/format.c does,
 * and returns the title, the author, the sections, the INFO lines and the cover.
 * It runs in a browser (window.bmCart) and in Node (module.exports).
 */
(function (root) {
  "use strict";

  var HEADER_SIZE = 128;
  var SECTION_NAMES = {
    1: "LUA", 2: "SHEET", 3: "MAP", 4: "COVER", 5: "SHEET8", 6: "AUDIO", 7: "ANIM (old)",
    8: "MESH", 9: "ANIM", 10: "INFO", 11: "SPRITES", 12: "LAYERS", 13: "FLAGS", 14: "BOXES"
  };
  var SIZES = ["640x360", "480x270", "320x180", "256x256", "360x360"];
  var crcTable = null;

  function crc32(u8, from, to) {
    if (!crcTable) {
      crcTable = new Uint32Array(256);
      for (var n = 0; n < 256; n++) {
        var c = n;
        for (var k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
        crcTable[n] = c >>> 0;
      }
    }
    var crc = 0xFFFFFFFF;
    for (var i = from; i < to; i++) crc = crcTable[(crc ^ u8[i]) & 0xFF] ^ (crc >>> 8);
    return (crc ^ 0xFFFFFFFF) >>> 0;
  }

  function text(u8, from, to) {
    var end = from;
    while (end < to && u8[end] !== 0) end++;
    return new TextDecoder("utf-8").decode(u8.subarray(from, end));
  }

  function ascii(u8, from, n) {
    var s = "";
    for (var i = 0; i < n; i++) s += String.fromCharCode(u8[from + i]);
    return s;
  }

  function fail(msg) {
    return { ok: false, error: msg };
  }

  /* The INFO section: "key: value" lines; a "[type name]" block ends the header lines. */
  function parseInfo(str) {
    var info = {};
    var lines = str.split(/\r?\n/);
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].trim();
      if (line.charAt(0) === "[") break;
      var at = line.indexOf(":");
      if (at > 0) info[line.slice(0, at).trim()] = line.slice(at + 1).trim();
    }
    return info;
  }

  /* data: ArrayBuffer or Uint8Array; name: the file name (the kind comes from the extension). */
  function parse(data, name) {
    var u8 = data instanceof Uint8Array ? data : new Uint8Array(data);
    var dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
    var len = u8.length;
    if (len < HEADER_SIZE) return fail("The file is too short for a .bm header.");
    var magic = ascii(u8, 0, 8);
    if (magic === "BMRES\0\0\0") return fail("This is a resource file, not a game cartridge.");
    if (magic !== "BMCART\0\0" && magic !== "BM33CART") return fail("The file is not a .bm cartridge (bad magic).");
    if (dv.getUint16(8, true) !== 1 || dv.getUint16(10, true) !== HEADER_SIZE)
      return fail("Unsupported .bm version.");
    var width = dv.getUint16(12, true), height = dv.getUint16(14, true);
    if (SIZES.indexOf(width + "x" + height) < 0) return fail("Unsupported cartridge size " + width + "x" + height + ".");
    if (u8[16] !== 1) return fail("Unsupported pixel format " + u8[16] + " (only RGB565).");
    var count = u8[17], flags = dv.getUint16(18, true);
    if (count * 16 > len - HEADER_SIZE) return fail("The section table is longer than the file.");

    var sections = [], byType = {};
    for (var i = 0; i < count; i++) {
      var at = HEADER_SIZE + i * 16;
      var type = dv.getUint32(at, true), off = dv.getUint32(at + 4, true), size = dv.getUint32(at + 8, true);
      if (off + size > len) return fail("Section " + type + " is outside the file.");
      var sec = { type: type, name: SECTION_NAMES[type] || "unknown", offset: off, size: size };
      sections.push(sec);
      byType[type] = sec;           /* the last one wins, as in the kernel */
    }
    if (!byType[1]) return fail("The cartridge has no LUA section.");

    var crcStored = dv.getUint32(20, true), crcActual = crc32(u8, HEADER_SIZE, len);
    var ext = /\.([A-Za-z0-9]+)$/.exec(name || "");
    var result = {
      ok: true,
      title: text(u8, 24, 72), author: text(u8, 72, 104), builtFrom: text(u8, 104, 120),
      width: width, height: height, flags: flags,
      project: !!(flags & 1), kind: ["game", "app", "utility", "game"][(flags >> 2) & 3],
      extension: ext ? ext[1].toLowerCase() : "",
      size: len, crcStored: crcStored, crcActual: crcActual, crcOk: crcStored === crcActual,
      sections: sections, info: {}, cover: null, lua: null
    };
    var s = byType[1];
    result.lua = new Uint8Array(u8.buffer, u8.byteOffset + s.offset, s.size);
    s = byType[10];
    if (s && s.size <= 16384) result.info = parseInfo(text(u8, s.offset, s.offset + s.size));
    s = byType[4];
    if (s && s.size >= 4) {
      var cw = dv.getUint16(s.offset, true), ch = dv.getUint16(s.offset + 2, true);
      if (cw && ch && cw <= 512 && ch <= 512 && 4 + cw * ch * 4 === s.size)
        result.cover = { width: cw, height: ch, rgba: u8.subarray(s.offset + 4, s.offset + s.size) };
    }
    return result;
  }

  var api = { parse: parse, crc32: crc32, SECTION_NAMES: SECTION_NAMES };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.bmCart = api;
})(typeof window !== "undefined" ? window : this);
