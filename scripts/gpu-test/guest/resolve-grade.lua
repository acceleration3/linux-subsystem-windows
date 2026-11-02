local GRADES = [[C:\av\media\resolve\grades\]]
LSW_PAGE = "edit"
dofile([[C:\av\apps\resolve-go.lua]])
local timeline = resolve:GetProjectManager():GetCurrentProject():GetCurrentTimeline()
local graph = timeline:GetItemListInTrack("video", 1)[1]:GetNodeGraph()
local drx = GRADES .. LSW_TIMELINE .. ".drx"
assert(bmd.fileexists(drx), "no " .. drx)
graph:ResetAllGrades()
assert(graph:ApplyGradeFromDRX(drx, 0), "ApplyGradeFromDRX " .. drx)
print(LSW_TIMELINE .. " graded from " .. drx .. ": " .. graph:GetNumNodes() .. " node(s)")
assert(resolve:GetProjectManager():SaveProject(), "SaveProject")
