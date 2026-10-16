-- resolve-go.lua: the state a resolve/* capture starts its feature from, in
-- Resolve's console (F6):
--   LSW_TIMELINE=[[grade]]; LSW_PAGE=[[color]]; dofile([[C:\av\apps\resolve-go.lua]])
-- loads the project resolve-setup.lua made (if it is not the open one),
-- makes LSW_TIMELINE current with its playhead at its start and shows
-- LSW_PAGE (media, cut, edit, fusion, color, fairlight, deliver), and
-- selects the media pool clip LSW_CLIP when it is set.
local pm = resolve:GetProjectManager()
local project = pm:GetCurrentProject()
if not project or project:GetName() ~= "lsw-mimic" then
    project = assert(pm:LoadProject("lsw-mimic"), "LoadProject lsw-mimic (run resolve-setup.lua first)")
end
local found
for i = 1, project:GetTimelineCount() do
    local timeline = project:GetTimelineByIndex(i)
    if timeline:GetName() == LSW_TIMELINE then found = timeline end
end
assert(found or not LSW_TIMELINE, "no timeline " .. tostring(LSW_TIMELINE))
if found then
    assert(project:SetCurrentTimeline(found), "SetCurrentTimeline")
    found:SetCurrentTimecode(found:GetStartTimecode())
end
if LSW_PAGE then assert(resolve:OpenPage(LSW_PAGE), "OpenPage " .. LSW_PAGE) end
-- LSW_CLIP: a media pool clip to select (the Media page's viewer shows it)
if LSW_CLIP then
    local pool = project:GetMediaPool()
    for _, clip in ipairs(pool:GetRootFolder():GetClipList()) do
        if clip:GetName() == LSW_CLIP then assert(pool:SetSelectedClip(clip), "SetSelectedClip " .. LSW_CLIP) end
    end
end
print("lsw-mimic: timeline " .. project:GetCurrentTimeline():GetName() .. ", page " .. resolve:GetCurrentPage())
