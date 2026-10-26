/*
 * player.js: the web player (docs/design/bm-web-player.md, stage 3). It defines window.bmLauncher:
 *   bmLauncher.start(cartUrl)           fetch a .bm and play it
 *   bmLauncher.startBytes(bytes, name)  play a cartridge already in memory (ArrayBuffer or Uint8Array)
 *   bmLauncher.stop()                   leave the cartridge
 * bmweb.js and bmweb.wasm (make web-player) are loaded when the first cartridge starts.
 * It draws on canvas#bm-canvas, reads the keyboard and the gamepad, and plays the sound with an
 * AudioWorklet at 48 kHz. The saves of the cartridges (the SD card's bm/save) live in IndexedDB. Nothing
 * leaves the browser.
 */
(function () {
  "use strict";

  /* HID_* bits of src/usb/hid.h */
  var B = { LEFT: 1, RIGHT: 2, UP: 4, DOWN: 8, A: 16, B: 32, START: 64, SELECT: 128, X: 256, Y: 512,
            L1: 1024, R1: 2048, L2: 4096, R2: 8192, L3: 16384, R3: 32768 };
  /* keyboard: the keys of the serial console of the runtime (space/j = A, k/x = B), the arrows */
  var KEYS = {
    ArrowLeft: B.LEFT, ArrowRight: B.RIGHT, ArrowUp: B.UP, ArrowDown: B.DOWN,
    Space: B.A, KeyJ: B.A, KeyK: B.B, KeyX: B.B, KeyU: B.X, KeyI: B.Y,
    Enter: B.START, ShiftRight: B.SELECT, Backspace: B.SELECT,
    KeyQ: B.L1, KeyE: B.R1, Digit1: B.L2, Digit3: B.R2
  };
  /* Gamepad API, "standard" mapping */
  var PAD = [B.A, B.B, B.X, B.Y, B.L1, B.R1, B.L2, B.R2, B.SELECT, B.START, B.L3, B.R3,
             B.UP, B.DOWN, B.LEFT, B.RIGHT];
  var FRAME_MS = 1000 / 60;

  var stats = { frames: 0, audioFrames: 0, audioState: "none", hz: 0, frameAvgMs: 0, frameWorstMs: 0,
                audioUnderruns: 0, audioDrops: 0, audioQueued: 0 };
  var mod = null, loading = null, running = false, held = 0, quitting = false;
  var ctx = null, audio = null, lastPad = "", dead = 0.15;

  function canvas() { return document.getElementById("bm-canvas"); }

  function loadModule() {
    if (mod) return Promise.resolve(mod);
    if (loading) return loading;
    loading = new Promise(function (resolve, reject) {
      var s = document.createElement("script");
      s.src = "bmweb.js";
      s.onload = function () {
        if (typeof createBmWeb !== "function") return reject(new Error("bmweb.js has no createBmWeb"));
        createBmWeb({ bmFrame: onFrame, bmAudio: onAudio, bmPace: onPace, bmDone: onDone }).then(function (m) {
          mod = m;
          return mountSaves(m);
        }).then(function () { resolve(mod); }, reject);
      };
      s.onerror = function () {
        loading = null;
        reject(new Error("The bm runtime (bmweb.js, bmweb.wasm) is not built. Run: make web-player"));
      };
      document.head.appendChild(s);
    });
    return loading;
  }

  /* ---- saves: /sd/bm/save is an IndexedDB file system; it is read before a start and written while a
   * cartridge runs (every 3 s) and when it ends */
  var saveDirty = false;
  function mountSaves(m) {
    var FS = m.FS;
    ["/sd", "/sd/bm", "/sd/bm/save"].forEach(function (d) { try { FS.mkdir(d); } catch (e) { /* exists */ } });
    if (typeof indexedDB === "undefined" || !m.IDBFS) return Promise.resolve();
    FS.mount(m.IDBFS, {}, "/sd/bm/save");
    saveDirty = true;
    return new Promise(function (resolve) {
      FS.syncfs(true, function (err) { if (err) console.warn("bm saves: cannot read IndexedDB", err); saveDirty = false; resolve(); });
    });
  }
  function flushSaves() {
    if (!mod || typeof indexedDB === "undefined" || !mod.IDBFS) return;
    mod.FS.syncfs(false, function (err) { if (err) console.warn("bm saves: cannot write IndexedDB", err); });
  }

  /* ---- video */
  function onFrame(ptr, w, h) {
    stats.frames++;
    var c = canvas();
    if (!c) return;
    if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
    if (!ctx || ctx.canvas !== c) ctx = c.getContext("2d");
    var px = new Uint8ClampedArray(mod.HEAPU8.buffer.slice(ptr, ptr + w * h * 4));
    ctx.putImageData(new ImageData(px, w, h), 0, 0);
  }

  /* ---- the 60 Hz clock (BmPacer, web/bmring.js): resolves when the next frame time has come */
  var pacer = new BmPacer(FRAME_MS, 250);       /* after a pause longer than 250 ms: do not run to catch up */
  function onPace() {
    pollInput();
    var wait = pacer.delay(performance.now());
    stats.hz = pacer.hz(); stats.frameAvgMs = pacer.avgMs(); stats.frameWorstMs = pacer.worst;
    /* a timer, not requestAnimationFrame: a hidden page keeps its clock (the browser may slow it down) */
    return new Promise(function (resolve) { setTimeout(resolve, wait); });
  }

  /* ---- sound: the worklet runs BmRing (web/bmring.js) as text; 14400 frames = 0.3 s at most in the queue */
  var WORKLET = BmRing.toString() + ";class P extends AudioWorkletProcessor{constructor(){super();this.r=new BmRing(14400);" +
    "this.port.onmessage=e=>{if(e.data===1)this.port.postMessage([this.r.under,this.r.drops,this.r.n]);else this.r.push(e.data)}}" +
    "process(i,out){this.r.pull(out[0][0],out[0][1]);return true}}registerProcessor('bm-audio',P)";

  function startAudio() {
    if (audio) { if (audio.ctx.state === "suspended") audio.ctx.resume(); return Promise.resolve(); }
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC || !window.AudioWorkletNode) return Promise.resolve();
    var c = new AC({ sampleRate: 48000, latencyHint: "interactive" });
    var url = URL.createObjectURL(new Blob([WORKLET], { type: "application/javascript" }));
    return c.audioWorklet.addModule(url).then(function () {
      var node = new AudioWorkletNode(c, "bm-audio", { outputChannelCount: [2] });
      node.port.onmessage = function (e) { stats.audioUnderruns = e.data[0]; stats.audioDrops = e.data[1]; stats.audioQueued = e.data[2]; };
      node.connect(c.destination);
      audio = { ctx: c, node: node };
    }).catch(function () { audio = null; });
  }

  function onAudio(ptr, n) {
    if (!audio) return;
    stats.audioFrames += n;
    stats.audioState = audio.ctx.state;
    var chunk = new Float32Array(mod.HEAPF32.buffer.slice(ptr, ptr + n * 8));
    audio.node.port.postMessage(chunk, [chunk.buffer]);
  }

  /* ---- input */
  function pollInput() {
    var bits = held, lx = 0, ly = 0, rx = 0, ry = 0;
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (var i = 0; i < pads.length; i++) {
      var p = pads[i];
      if (!p || p.mapping !== "standard") continue;
      for (var b = 0; b < PAD.length && b < p.buttons.length; b++) if (p.buttons[b].pressed) bits |= PAD[b];
      var ax = function (k) { return Math.abs(p.axes[k] || 0) < dead ? 0 : p.axes[k]; };
      lx = ax(0); ly = ax(1); rx = ax(2); ry = ax(3);
      break;
    }
    if (bits & B.LEFT && !lx) lx = -1;
    if (bits & B.RIGHT && !lx) lx = 1;
    if (bits & B.UP && !ly) ly = -1;
    if (bits & B.DOWN && !ly) ly = 1;
    var sig = bits + "," + lx + "," + ly + "," + rx + "," + ry;
    if (sig !== lastPad) {
      lastPad = sig;
      mod._bmweb_input(bits, lx, ly, rx, ry);
    }
  }

  window.addEventListener("keydown", function (ev) {
    if (!running) return;
    if (ev.code === "Escape") { stop(); ev.preventDefault(); return; }
    if (KEYS[ev.code]) { held |= KEYS[ev.code]; ev.preventDefault(); }
  });
  window.addEventListener("keyup", function (ev) {
    if (KEYS[ev.code]) held &= ~KEYS[ev.code];
  });
  window.addEventListener("blur", function () { held = 0; });

  /* ---- control */
  var finish = null, saveTimer = 0;
  function onDone() {
    running = false;
    clearInterval(saveTimer);
    flushSaves();
    if (finish) finish();
    if (window.bmLauncher.onstop) window.bmLauncher.onstop();
  }
  function stop() {
    if (running && mod) mod._bmweb_quit();
  }

  function startBytes(data, name) {
    if (running) return Promise.reject(new Error("A cartridge is already running."));
    var bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    return Promise.all([loadModule(), startAudio()]).then(function (r) {
      var m = r[0];
      var ptr = m._malloc(bytes.length);
      m.HEAPU8.set(bytes, ptr);
      running = true; held = 0; lastPad = ""; pacer.reset();
      m._bmweb_input(0, 0, 0, 0, 0);
      saveTimer = setInterval(function () { flushSaves(); if (audio) audio.node.port.postMessage(1); }, 3000);
      /* bmweb_start sleeps between frames (Asyncify): it returns at once and bmDone tells the end */
      return new Promise(function (resolve) {
        finish = function () { m._free(ptr); resolve(); };
        m._bmweb_start(ptr, bytes.length);
      });
    });
  }

  function start(cartUrl) {
    return fetch(cartUrl).then(function (r) {
      if (!r.ok) throw new Error("Cannot load " + cartUrl + " (" + r.status + ")");
      return r.arrayBuffer();
    }).then(function (buf) { return startBytes(buf, cartUrl); });
  }

  window.bmLauncher = { start: start, startBytes: startBytes, stop: stop, isRunning: function () { return running; }, stats: stats,
                        saveFiles: function () {
                          var o = {};
                          if (mod) mod.FS.readdir("/sd/bm/save").forEach(function (n) { if (n[0] !== ".") o[n] = mod.FS.readFile("/sd/bm/save/" + n, { encoding: "utf8" }); });
                          return o;
                        },
                        onstop: null, onerror: null };
})();
