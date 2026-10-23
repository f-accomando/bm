/*
 * bmring.js: the two timing parts of the web player, apart from the browser so that Node can test them
 * (docs/design/bm-web-player.md, stage 4).
 *   BmPacer(frameMs, maxLagMs)  the 60 Hz clock: delay(nowMs) gives the wait before the next frame
 *   BmRing(maxFrames)           the sound queue of the AudioWorklet: push() 48 kHz stereo floats, pull() them
 * BmRing is self contained: player.js copies its source (BmRing.toString()) into the worklet.
 * The browser loads this file as a script; Node loads it with require().
 */
class BmRing {
  constructor(maxFrames) {
    this.q = [];            /* queue of Float32Array chunks, interleaved left/right */
    this.o = 0;             /* read position in the first chunk, in frames */
    this.n = 0;             /* frames in the queue */
    this.max = maxFrames;
    this.drops = 0;         /* chunks dropped because the queue was too long */
    this.under = 0;         /* process() blocks that ended with silence */
  }
  push(chunk) {
    this.q.push(chunk);
    this.n += chunk.length / 2;
    while (this.n > this.max && this.q.length > 1) {      /* too late: drop the oldest sound */
      this.n -= this.q.shift().length / 2 - this.o;
      this.o = 0;
      this.drops++;
    }
  }
  pull(l, r) {
    var k = 0;
    while (k < l.length && this.q.length) {
      var c = this.q[0];
      l[k] = c[this.o * 2];
      r[k] = c[this.o * 2 + 1];
      k++;
      this.n--;
      if (++this.o * 2 >= c.length) { this.q.shift(); this.o = 0; }
    }
    if (k < l.length) {
      this.under++;
      for (; k < l.length; k++) { l[k] = 0; r[k] = 0; }
    }
  }
}

class BmPacer {
  constructor(frameMs, maxLagMs) {
    this.frameMs = frameMs;
    this.maxLag = maxLagMs;
    this.next = 0;
    this.last = 0;
    this.count = 0;         /* intervals measured */
    this.sum = 0;           /* ms */
    this.worst = 0;         /* ms */
    this.resets = 0;        /* times the clock gave up catching up */
  }
  reset() { this.next = 0; this.last = 0; }
  /* ms to wait before the next frame; call once per frame, with the time at the call */
  delay(now) {
    if (this.last) {
      var d = now - this.last;
      this.count++; this.sum += d;
      if (d > this.worst) this.worst = d;
    }
    this.last = now;
    if (!this.next || now > this.next + this.maxLag) { if (this.next) this.resets++; this.next = now; }
    this.next += this.frameMs;
    return Math.max(0, this.next - now - 1);
  }
  avgMs() { return this.count ? this.sum / this.count : 0; }
  hz() { return this.count ? 1000 * this.count / this.sum : 0; }
}

if (typeof module !== "undefined") module.exports = { BmRing: BmRing, BmPacer: BmPacer };
