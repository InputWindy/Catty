-- Catty game script entry (loaded as Scripts/main.lua on engine init).
-- Optional globals called from FApp:
--   OnUpdate(dt)
--   OnFixedUpdate(fixedDt)

catty.log("Scripts/main.lua loaded")
catty.log_warn("Scripts/main.lua loaded")
catty.log_error("Scripts/main.lua loaded")

local Accumulated = 0.0

function OnUpdate(dt)
	Accumulated = Accumulated + dt
	-- Uncomment to spam the log while testing:
	-- catty.log(string.format("OnUpdate dt=%.4f acc=%.2f", dt, Accumulated))
end

function OnFixedUpdate(fixedDt)
	-- physics / fixed-step gameplay
end
