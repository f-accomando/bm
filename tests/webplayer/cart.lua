-- Fixture of the web player test (tests/webplayer/run_wasm.py): fixed 2D drawing,
-- the same frame on the PC build and on the WebAssembly build of bmhost.
-- Colours are 0xRRGGBB.
function _draw()
  cls(0x102040)
  rectfill(10, 10, 100, 60, 0xFF4040)
  rectfill(120, 20, 200, 90, 0x40FF40)
  print("bm web", 12, 70, 0xFFFFFF)
end
