-- test build (TAPE_TEST=@N@): log each note the sequencer queues, and update the sequencer every @N@-th frame
seq_skip = @N@
at_hook = function(t, track, note, vel, tick)
  log(string.format("TAPE_AT %.6f %d %d %d %d", t, track, note, vel, tick))
end
