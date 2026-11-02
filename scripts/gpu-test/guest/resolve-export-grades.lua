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
