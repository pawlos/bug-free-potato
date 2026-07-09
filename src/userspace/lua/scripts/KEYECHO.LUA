-- Echo key codes for ~5 seconds, then exit. Proves potatos.read_key + timing.
-- Needs a focused window so keystrokes reach this task (not the kernel shell).
local p = require("potatos")
p.open(400, 200, "Key Echo")
print("keyecho: window " .. p.fb_width() .. "x" .. p.fb_height())
print("press keys in the window for 5s...")
local stop = p.micros() + 5000000
while p.micros() < stop do
  local k = p.read_key()
  if k then
    local printable = (k >= 32 and k < 127) and string.char(k) or "?"
    print("key", k, printable)
  end
  p.sleep(10)
end
print("keyecho done")
