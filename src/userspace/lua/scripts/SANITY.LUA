-- Exercises the standard library and confirms unsupported OS calls fail
-- gracefully (never crash). Prints "SANITY OK" on success.
assert(math.floor(math.sqrt(144)) == 12, "math")
assert(string.upper("abc") == "ABC", "string")
assert(#({1, 2, 3, 4}) == 4, "table len")
local t = {3, 1, 2}; table.sort(t)
assert(t[1] == 1 and t[3] == 3, "table.sort")
assert(select("#", 1, 2, 3) == 3, "select")
local co = coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i) end end)
assert(co() + co() + co() == 6, "coroutine")
assert(type(os.time()) == "number", "os.time")
assert(utf8.len("hello") == 5, "utf8")
print("stdlib OK")

-- Unsupported calls must fail gracefully (nil/false/error), not hang or panic.
local ok = os.execute("whatever")
print("os.execute ->", tostring(ok))
local f = io.popen and select(2, pcall(io.popen, "x")) or "absent"
print("io.popen   ->", tostring(f))
local lib, lerr = package.loadlib("x.so", "y")
print("loadlib    ->", tostring(lib), tostring(lerr))

print("SANITY OK")
