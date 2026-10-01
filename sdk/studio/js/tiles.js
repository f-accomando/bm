/*
 * bm Studio - the starter sprite sheet of a new project: 16x16 tiles
 * drawn in code (grass, stone, bricks, wood, roof, windows, a door,
 * plants...), so the 3D view has something to build with at once.
 */
(function (root) {
  'use strict';
  const BM = root.BM;

  function rng(seed) {
    let s = seed >>> 0 || 1;
    return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
  }

  const hex = c => [c >> 16 & 255, c >> 8 & 255, c & 255];
  const pick = (r, list) => list[Math.floor(r() * list.length) % list.length];

  /* tile at column tx, row ty: fn(x, y, r) -> 0xRRGGBB, or null = transparent */
  function tile(img, tx, ty, seed, fn) {
    const r = rng(seed);
    for (let y = 0; y < 16; y++)
      for (let x = 0; x < 16; x++) {
        const c = fn(x, y, r), o = ((ty * 16 + y) * img.w + tx * 16 + x) * 4;
        if (c == null) { img.px[o + 3] = 0; continue; }
        const [R, G, B] = hex(c);
        img.px[o] = R; img.px[o + 1] = G; img.px[o + 2] = B; img.px[o + 3] = 255;
      }
  }

  const GRASS = [0x3E8A3A, 0x4E9E3E, 0x5CB048, 0x357A34];
  const DIRT = [0x7A5230, 0x8A6038, 0x6B4428, 0x94703F];
  const STONE = [0x80828C, 0x8E909A, 0x70727C, 0x9A9CA6];
  const WOOD = [0xA0703A, 0xB07E44, 0x8E6232];

  function starterSheet() {
    const img = BM.newImage(256, 256);
    // row 0: ground and walls
    tile(img, 0, 0, 1, (x, y, r) => pick(r, GRASS));                                   // grass
    tile(img, 1, 0, 2, (x, y, r) => pick(r, DIRT));                                    // dirt
    tile(img, 2, 0, 3, (x, y, r) => y < 3 + ((x * 7) % 3) ? pick(r, GRASS) : pick(r, DIRT)); // grass side
    tile(img, 3, 0, 4, (x, y, r) => pick(r, STONE));                                   // stone
    tile(img, 4, 0, 5, (x, y, r) => {                                                  // cobbles
      const cx = (x + (y >> 2) * 3) & 7, cy = y & 7;
      return cx === 0 || cy === 0 ? 0x55565E : pick(r, STONE);
    });
    const brick = (base, mortar, moss) => (x, y, r) => {
      const row = y >> 2, bx = (x + (row & 1) * 4) & 7;
      if ((y & 3) === 3 || bx === 7) return mortar;
      if (moss && r() < 0.18 + (y > 10 ? 0.25 : 0)) return pick(r, [0x4E7A30, 0x5C8A38]);
      return pick(r, base);
    };
    tile(img, 5, 0, 6, brick([0xA84632, 0xB8503A, 0x983E2C], 0xD8C8B0));             // red bricks
    tile(img, 6, 0, 7, brick([0x8A8A70, 0x9A9A7C, 0x7A7A64], 0x5A5A48, true));          // mossy bricks
    tile(img, 7, 0, 8, (x, y, r) => (y & 3) === 3 ? 0x6A4A26 : ((x + (y >> 2) * 5) % 16 === 0 ? 0x7A5630 : pick(r, WOOD))); // planks
    tile(img, 8, 0, 9, (x, y, r) => (x % 5 === 0 ? 0x6A4626 : pick(r, [0x8A5A30, 0x7E522C, 0x946236]))); // log side
    tile(img, 9, 0, 10, (x, y, r) => {                                                 // log top
      const d = Math.hypot(x - 7.5, y - 7.5);
      if (d > 7.6) return 0x6A4626;
      return Math.floor(d) % 3 === 0 ? 0x9A6E3E : pick(r, [0xC49A5E, 0xB88E54]);
    });
    tile(img, 10, 0, 11, (x, y, r) => r() < 0.12 ? null : pick(r, [0x2E6E2A, 0x3A8434, 0x286024, 0x4C9A3E])); // leaves
    tile(img, 11, 0, 12, (x, y, r) => ((x + y * 2) % 9 === 0 || (x * 3 + y) % 13 === 0) ? 0x7ABCE8 : pick(r, [0x2E6EB8, 0x3478C4, 0x2A64A8])); // water
    tile(img, 12, 0, 13, (x, y, r) => pick(r, [0xE0C888, 0xD8BE7C, 0xE8D294]));       // sand
    tile(img, 13, 0, 14, (x, y, r) => pick(r, [0xF0F4FA, 0xE4EAF2, 0xFFFFFF]));       // snow
    tile(img, 14, 0, 15, (x, y, r) => {                                                // roof tiles
      const row = y >> 2, bx = (x + (row & 1) * 4) & 7, yy = y & 3;
      if (yy === 3) return 0x5A2A22;
      return bx === 0 ? 0x7A3428 : (yy === 0 ? 0xB85A44 : pick(r, [0xA04A38, 0x9A4434]));
    });
    tile(img, 15, 0, 16, (x, y, r) => {                                                // metal plate
      if (x === 0 || y === 0) return 0xB8BCC8;
      if (x === 15 || y === 15) return 0x50545E;
      if ((x === 2 || x === 13) && (y === 2 || y === 13)) return 0x3A3E46;
      return pick(r, [0x8A8E9A, 0x868A96]);
    });
    // row 1: details
    tile(img, 0, 1, 21, (x, y) => {                                                    // window
      if (x < 1 || x > 14 || y < 1 || y > 14) return 0x6A4A26;
      if (x === 7 || x === 8 || y === 7 || y === 8) return 0x8A6436;
      return (x + y) % 11 === 0 || (x + y) % 11 === 1 ? 0xDAF0FF : 0x6AA8D8;
    });
    const door = top => (x, y, r) => {
      if (x < 1 || x > 14 || (top && y < 1)) return 0x4A3018;
      if (top && y < 5 && Math.hypot(x - 7.5, y - 6) > 6.5) return 0x4A3018;
      if (!top && y === 6 && x === 11) return 0xE8C040;
      return x % 4 === 3 ? 0x6A4424 : pick(r, [0x8A5A30, 0x94643A]);
    };
    tile(img, 1, 1, 22, door(true));                                                   // door top
    tile(img, 1, 2, 23, door(false));                                                  // door bottom
    tile(img, 2, 1, 24, (x, y, r) => {                                                 // crate
      if (x < 2 || x > 13 || y < 2 || y > 13) return 0x7A5228;
      if (x === y || x === 15 - y || x === y + 1 || x === 16 - y) return 0x7A5228;
      return pick(r, [0xB88A4E, 0xC49658]);
    });
    tile(img, 3, 1, 25, (x, y) => (x === 0 || y === 0 || x === 15 || y === 15) ? 0xA8C8E0 : null); // glass frame
    tile(img, 4, 1, 26, (x, y, r) => {                                                 // flowers
      const stem = (x === 4 && y > 7) || (x === 11 && y > 5) || (x === 8 && y > 10);
      if (stem) return 0x3A8434;
      const heads = [[4, 6, 0xE84A5A], [11, 4, 0xF0C838], [8, 9, 0xB060D8]];
      for (const [hx, hy, c] of heads) if (Math.abs(x - hx) + Math.abs(y - hy) <= 1) return (x === hx && y === hy) ? 0xFFF4C0 : c;
      return y > 13 && r() < 0.6 ? pick(r, GRASS) : null;
    });
    tile(img, 5, 1, 27, (x, y, r) => {                                                 // bush
      const d = Math.hypot(x - 7.5, (y - 9) * 1.3);
      return d < 7 && r() > d / 10 ? pick(r, [0x2E6E2A, 0x3A8434, 0x4C9A3E]) : null;
    });
    tile(img, 6, 1, 28, (x, y) => {                                                    // fence
      if ((x === 2 || x === 3 || x === 12 || x === 13) && y > 1) return x % 2 ? 0x8A5A30 : 0xA0703A;
      if ((y === 5 || y === 6 || y === 11 || y === 12)) return y % 2 ? 0x8A5A30 : 0xA0703A;
      return null;
    });
    tile(img, 7, 1, 29, (x, y) => {                                                    // ladder
      if (x === 2 || x === 3 || x === 12 || x === 13) return x % 2 ? 0x7A5228 : 0x94643A;
      if (y % 5 === 2 && x > 3 && x < 12) return 0xA0703A;
      return null;
    });
    tile(img, 8, 1, 30, (x, y, r) => {                                                 // lava
      const v = Math.sin(x * 0.8 + y * 0.3) + Math.cos(y * 0.9 - x * 0.2);
      return v > 1 ? 0xFFE060 : v > 0 ? pick(r, [0xF08020, 0xF89028]) : pick(r, [0xC03818, 0xB03010]);
    });
    tile(img, 9, 1, 31, (x, y, r) => {                                                 // gravel path
      return r() < 0.2 ? pick(r, [0x5A5048, 0xA89C8C]) : pick(r, [0x8A7E70, 0x7E7266, 0x968A7A]);
    });
    tile(img, 10, 1, 32, (x, y, r) => {                                                // tree trunk (sprite)
      if (x < 5 || x > 10) return null;
      return x === 5 || x === 10 ? 0x5A3A1E : pick(r, [0x7A5228, 0x6E4A24]);
    });
    tile(img, 11, 1, 33, (x, y, r) => {                                                // tree top (sprite)
      const d = Math.hypot(x - 7.5, y - 8);
      return d < 7.5 && r() > (d - 5) / 3 ? pick(r, [0x2E7A2E, 0x3A8C36, 0x266A28, 0x4CA040]) : null;
    });
    tile(img, 12, 1, 34, (x, y, r) => {                                                // carpet
      if (x === 1 || y === 1 || x === 14 || y === 14) return 0xE8C040;
      return (x + y) % 4 === 0 ? 0xA02838 : pick(r, [0xC03448, 0xB82E40]);
    });
    tile(img, 13, 1, 35, (x, y, r) => pick(r, [0xD8D0C0, 0xE0D8C8, 0xCCC4B2]));      // plaster
    tile(img, 14, 1, 36, (x, y) => (x + y) % 8 < 4 ? 0x303038 : 0xE8E8F0);            // checker
    tile(img, 15, 1, 37, (x, y, r) => {                                                // bm logo tile
      const L = ['................', '................', '.##.....#...#...', '.#.#....##.##...', '.##.....#.#.#...',
        '.#.#....#...#...', '.##.....#...#...', '................'];
      const row = L[Math.floor(y / 2)] || '';
      return row[x] === '#' ? 0xFFC050 : pick(r, [0x1C2030, 0x202438]);
    });
    // row 2 (after the door's lower half): plain colours to paint with
    const plain = [0xFFFFFF, 0xC0C4D0, 0x808490, 0x404450, 0x101218, 0xE84A5A, 0xF09030, 0xF0D040,
      0x5CB048, 0x2E8A70, 0x3478C4, 0x6A50C8, 0xB060D8, 0xE890B0, 0x8A5A30];
    plain.forEach((c, i) => tile(img, i + 2, 2, 40 + i, () => c));
    return img;
  }

  BM.starterSheet = starterSheet;
  BM.STARTER = {
    grass: [0, 0], dirt: [1, 0], grassSide: [2, 0], stone: [3, 0], cobbles: [4, 0], bricks: [5, 0], mossy: [6, 0],
    planks: [7, 0], log: [8, 0], logTop: [9, 0], leaves: [10, 0], water: [11, 0], sand: [12, 0], snow: [13, 0],
    roof: [14, 0], metal: [15, 0], window: [0, 1], doorTop: [1, 1], doorBottom: [1, 2], crate: [2, 1],
    glass: [3, 1], flowers: [4, 1], bush: [5, 1], fence: [6, 1], ladder: [7, 1], lava: [8, 1], gravel: [9, 1],
    trunk: [10, 1], treeTop: [11, 1], carpet: [12, 1], plaster: [13, 1], checker: [14, 1], logo: [15, 1],
  };
})(typeof window !== 'undefined' ? window : globalThis);
