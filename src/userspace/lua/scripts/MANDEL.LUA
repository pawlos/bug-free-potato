-- Mandelbrot set rendered into a potatos window via potatos.fill_rect.
-- Proves potatos framebuffer drawing + real floating-point math from Lua.
local p = require("potatos")
p.open(640, 400, "Mandelbrot")            -- focused window; drawing goes here
local W, H = p.fb_width(), p.fb_height()  -- window client size
local maxit = 32
local step = 2                            -- 2px blocks keep it fast
for py = 0, H - 1, step do
  local y0 = (py / H) * 2.0 - 1.0
  for px = 0, W - 1, step do
    local x0 = (px / W) * 3.0 - 2.0
    local x, y, it = 0.0, 0.0, 0
    while x * x + y * y <= 4.0 and it < maxit do
      x, y = x * x - y * y + x0, 2 * x * y + y0
      it = it + 1
    end
    local c = (it == maxit) and 0x000000 or (it * 0x080410) % 0xFFFFFF
    p.fill_rect(px, py, step, step, c)
  end
end
print("mandelbrot done - press a key in the window to exit")
while not p.read_key() do p.sleep(20) end  -- keep the window up until a keypress
