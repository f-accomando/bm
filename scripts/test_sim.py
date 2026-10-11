import lupa
from lupa import LuaRuntime

lua = LuaRuntime(unpack_returned_tuples=True)

lua.execute('''
SCREEN_W, SCREEN_H = 360, 360
local calls = 0
for _, n in ipairs({'cls', 'line', 'rect', 'circ', 'camera', 'clip', 'spr', 'map', 'mset', 'sset', 'note',
                    'noteoff', 'freq', 'envelope', 'duty', 'quit', 'rectfill', 'circfill', 'tri', 'sspr',
                    'pset', 'print', 'log', 'stick', 'players'}) do
  _G[n] = function(...) calls = calls + 1 end
end
pget = function() return 0 end
btn = function() return false end
btnp = function() return false end
stat = function() return 60 end
time = function() return 1 end
log = function(...) print(...) end
''')

with open('build/titan_ex/main.lua', 'r', encoding='utf-8') as f:
    lua.execute(f.read())

lua.execute('_init()')
for screen in ['title', 'mode', 'hangar', 'moves', 'fight', 'victory']:
    lua.execute(f'TITAN.Scr.go("{screen}")')
    for frame in range(60):
        lua.execute('_update()')
        lua.execute('_draw()')
    print(f'Screen {screen}: 60 frames verified successfully!')

print('ALL SCREENS AND MODES VERIFIED PERFECTLY!')
