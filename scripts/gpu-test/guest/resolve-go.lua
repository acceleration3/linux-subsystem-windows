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
if LSW_CLIP then
    local pool = project:GetMediaPool()
    for _, clip in ipairs(pool:GetRootFolder():GetClipList()) do
        if clip:GetName() == LSW_CLIP then assert(pool:SetSelectedClip(clip), "SetSelectedClip " .. LSW_CLIP) end
    end
end
print("lsw-mimic: timeline " .. project:GetCurrentTimeline():GetName() .. ", page " .. resolve:GetCurrentPage())
