-- resolve-setup.lua: the project the resolve/* traces are recorded in
-- (TRACES.md, "The Resolve suite"), built by Resolve's own scripting API.
-- Run it in Resolve's console (Workspace > Console, F6) with a project
-- open:  dofile([[C:\av\apps\resolve-setup.lua]])
-- It replaces the project "lsw-mimic" (the media from resolve-media.sh in
-- C:\av\media\resolve) and leaves it open:
--
--   timeline   what is on it
--   edit       pattern.mp4, bars.mov, fractal.mov, still.png cut back to back; tone.wav on A1
--   dissolve   pattern.mp4 then fractal.mov, a 1 s Cross Dissolve centred on the cut
--   title      a Text+ title on V1, pattern.mp4 over it on V2 in Screen composite mode
--   grade, nodes, qualifier, fx-blur, fx-glow, fx-vignette, fx-sharpen
--              one clip each, for resolve-grade.lua to grade from grades\<timeline>.drx
--              (nodes stays ungraded: its capture grades it)
--   fusion     bars.mov with a Fusion composition: Background -> Transform -> Merge over the clip
--   audio      tone.wav alone (Fairlight)
local MEDIA = [[C:\av\media\resolve\]]
local NAME = "lsw-mimic"

local pm = resolve:GetProjectManager()
resolve:OpenPage("media")
local current = pm:GetCurrentProject()
if current and current:GetName() == NAME then
    pm:CloseProject(current)
end
pm:DeleteProject(NAME)
local project = assert(pm:CreateProject(NAME), "CreateProject " .. NAME)
assert(project:SetSetting("timelineResolutionWidth", "1280"), "width")
assert(project:SetSetting("timelineResolutionHeight", "720"), "height")
assert(project:SetSetting("timelineFrameRate", "25"), "frame rate")
project:SetSetting("timelinePlaybackFrameRate", "25")

local pool = project:GetMediaPool()
local clips = {}
for _, name in ipairs({ "pattern.mp4", "bars.mov", "fractal.mov", "still.png", "tone.wav" }) do
    local imported = pool:ImportMedia({ MEDIA .. name })
    assert(imported and imported[1], "ImportMedia " .. name)
    clips[name] = imported[1]
end

local function timeline(name, items)
    local made = assert(pool:CreateTimelineFromClips(name, items), "timeline " .. name)
    print("timeline " .. name .. ": " .. made:GetEndFrame() - made:GetStartFrame() .. " frames")
    return made
end

local function whole(name) return { mediaPoolItem = clips[name] } end
local function part(name, first, last) return { mediaPoolItem = clips[name], startFrame = first, endFrame = last } end

-- edit: four cuts and the tone under them
local edit = timeline("edit", { whole("pattern.mp4"), whole("bars.mov"), whole("fractal.mov"), part("still.png", 0, 49) })
project:SetCurrentTimeline(edit)
pool:AppendToTimeline({ { mediaPoolItem = clips["tone.wav"], mediaType = 2, trackIndex = 1, recordFrame = edit:GetStartFrame() } })

-- dissolve: handles on both sides of the cut so the transition can centre on it
local dissolve = timeline("dissolve", { part("pattern.mp4", 0, 74), part("fractal.mov", 25, 99) })
local first = dissolve:GetItemListInTrack("video", 1)[1]
assert(first:AddTransition({ type = "Cross Dissolve", category = "simple", position = "end", alignment = "center", duration = 25 }),
    "Cross Dissolve")

-- title: Text+ under the pattern, which is composited over it in Screen
-- mode (the API inserts a title only on the destination track, V1)
local title = assert(pool:CreateEmptyTimeline("title"), "timeline title")
project:SetCurrentTimeline(title)
title:SetCurrentTimecode(title:GetStartTimecode())
local text = assert(title:InsertFusionTitleIntoTimeline("Text+"), "Text+")
local comp = text:GetFusionCompByIndex(1)
local template = comp and comp:FindTool("Template")
if template then
    template:SetInput("StyledText", "LSW mimic: Text+")
    template:SetInput("Size", 0.12)
end
title:AddTrack("video")
local over = pool:AppendToTimeline({ { mediaPoolItem = clips["pattern.mp4"], trackIndex = 2, recordFrame = title:GetStartFrame() } })
assert(over and over[1], "pattern over the title")
assert(over[1]:SetProperties({ CompositeMode = resolve.COMPOSITE_SCREEN }), "Screen")
print("timeline title: " .. title:GetEndFrame() - title:GetStartFrame() .. " frames")

-- the Color page's features, each on a timeline of its own; the grade of
-- each is grades\<timeline>.drx, applied afterwards one timeline per console
-- command by resolve-grade.lua (made by hand on the Color page once, grabbed
-- as a still and exported by resolve-export-grades.lua, kept in
-- scripts/gpu-test/guest/resolve-grades; TRACES.md, "The Resolve suite")
local graded = {
    { "grade", "fractal.mov" },         -- primaries: lift/gamma/gain wheels, contrast, a custom S curve
    { "nodes", "fractal.mov" },         -- no grade: its capture adds three serial nodes live (offset, contrast, gamma)
    { "qualifier", "pattern.mp4" },     -- an HSL qualifier (the yellow bar) and a circular power window, gain and offset in them
    { "fx-blur", "fractal.mov" },       -- Resolve FX Gaussian Blur
    { "fx-glow", "fractal.mov" },       -- Resolve FX Glow
    { "fx-vignette", "fractal.mov" },   -- Resolve FX Vignette
    { "fx-sharpen", "fractal.mov" },    -- the Blur palette's Sharpen (the free edition's Resolve FX Sharpen is Studio's)
}
for _, entry in ipairs(graded) do
    timeline(entry[1], { whole(entry[2]) })
end

-- fusion: Background -> Transform -> Merge (foreground) over the clip
local fusion_tl = timeline("fusion", { whole("bars.mov") })
local item = fusion_tl:GetItemListInTrack("video", 1)[1]
comp = item:GetFusionCompByIndex(1) or item:AddFusionComp()
comp:Lock()
local media_in, media_out = comp:FindTool("MediaIn1"), comp:FindTool("MediaOut1")
local background = comp:AddTool("Background", -32768, -32768)
background:SetInput("TopLeftRed", 0.9)
background:SetInput("TopLeftGreen", 0.4)
background:SetInput("TopLeftBlue", 0.1)
background:SetInput("Width", 640)
background:SetInput("Height", 360)
local transform = comp:AddTool("Transform", -32768, -32768)
transform:SetInput("Angle", 30)
transform:SetInput("Size", 0.8)
transform.Input = background.Output
local merge = comp:AddTool("Merge", -32768, -32768)
merge.Background = media_in.Output
merge.Foreground = transform.Output
merge:SetInput("Blend", 0.7)
media_out.Input = merge.Output
comp:Unlock()

-- audio: the tone alone, for Fairlight
timeline("audio", { whole("tone.wav") })

project:SetCurrentTimeline(edit)
assert(pm:SaveProject(), "SaveProject")
print("lsw-mimic: " .. project:GetTimelineCount() .. " timelines, saved")
