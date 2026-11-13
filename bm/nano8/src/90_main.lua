-- The cartridge: the keyboard is read key by key (so it can be mapped),
-- the settings come from the SD card, the list of carts opens.

function _init()
  rawkeys(true)
  Cfg.load()
  Ui.scan()
  Ui.go("browser")
end

function _update()
  Ui.update()
end

function _draw()
  Ui.draw()
end

-- the modules, for the tests on the PC (tests/nano8)
NANO8 = { Xl = Xl, Env = Env, In = In, Vm = Vm, Ui = Ui, Cfg = Cfg }
