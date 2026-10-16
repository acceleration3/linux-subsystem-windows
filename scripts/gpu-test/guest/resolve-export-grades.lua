-- resolve-export-grades.lua: each graded timeline's grade (resolve-setup.lua's
-- `graded` list), done by hand on the Color page, grabbed as a still and
-- exported as a DRX into C:\av\media\resolve\grades\export\<timeline>\ (which
-- must exist: Resolve's console Lua cannot make directories) as
-- <timeline>_1.1.1.drx; renamed <timeline>.drx it is what resolve-setup.lua
-- applies when it builds the project again.
local OUT = [[C:\av\media\resolve\grades\export\]]
local project = resolve:GetProjectManager():GetCurrentProject()
local album = project:GetGallery():GetCurrentStillAlbum()
local wanted = { grade = 1, qualifier = 1, ["fx-blur"] = 1, ["fx-glow"] = 1, ["fx-vignette"] = 1,
                 ["fx-sharpen"] = 1 }
resolve:OpenPage("color")
for i = 1, project:GetTimelineCount() do
    local timeline = project:GetTimelineByIndex(i)
    local name = timeline:GetName()
    if wanted[name] then
        project:SetCurrentTimeline(timeline)
        timeline:SetCurrentTimecode(timeline:GetStartTimecode())
        local nodes = timeline:GetItemListInTrack("video", 1)[1]:GetNodeGraph():GetNumNodes()
        local still = timeline:GrabStill()
        local ok = still and album:ExportStills({ still }, OUT .. name, name, "drx")
        print(name .. ": " .. nodes .. " node(s), exported " .. tostring(ok))
    end
end
