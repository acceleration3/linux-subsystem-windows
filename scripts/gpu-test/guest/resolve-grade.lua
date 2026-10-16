-- resolve-grade.lua: one graded timeline of lsw-mimic given its grade, in
-- Resolve's console (F6), from the Edit page:
--   LSW_TIMELINE=[[qualifier]]; dofile([[C:\av\apps\resolve-grade.lua]])
-- applies C:\av\media\resolve\grades\<timeline>.drx to the clip on V1.
-- One timeline per Resolve session, saved and quit after: applying them one
-- after another in one session crashed Resolve 21.1 (its UI thread calling
-- address 1), and so did a three-node DRX on a fresh clip, every time - the
-- nodes timeline is graded live in its capture instead.
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
