-- Passive session registration. No SavedVariables, account names or generated gameplay input.
local frame = CreateFrame("Frame", "WaiObserverIdentity", UIParent)
frame:SetFrameStrata("TOOLTIP")
frame:SetWidth(64)
frame:SetHeight(24)
frame:SetPoint("TOPLEFT", UIParent, "TOPLEFT", 8, -8)
local cells = {}
for i = 0, 95 do
    local cell = frame:CreateTexture(nil, "OVERLAY")
    cell:SetWidth(4)
    cell:SetHeight(4)
    cell:SetPoint("TOPLEFT", frame, "TOPLEFT", (i % 16) * 4, -math.floor(i / 16) * 4)
    cells[i + 1] = cell
end
local marker, character, fingerprint, elapsed = nil, nil, nil, 1
local function bits(number, count, result)
    for i = count - 1, 0, -1 do
        result[#result + 1] = math.floor(number / 2 ^ i) % 2
    end
end
local function paint(token)
    local result, checksum = {}, 0
    bits(42707, 16, result) -- A6D3, framing independent of character identity.
    for i = 1, 8 do
        local byte = tonumber(string.sub(token, i * 2 - 1, i * 2), 16)
        checksum = (checksum + i * byte) % 65536
        bits(byte, 8, result)
    end
    bits(checksum, 16, result)
    for i = 1, 96 do
        local value = result[i]
        cells[i]:SetTexture(value, value, value, 1)
    end
end
local function key(action)
    local value = GetBindingKey(action)
    if not value then return 0 end
    if #value == 1 and string.match(value, "^[A-Z]$") then return string.byte(value) end
    return ({ LEFT = 37, UP = 38, RIGHT = 39, DOWN = 40 })[value] or 0
end
frame:SetScript("OnUpdate", function(self, seconds)
    elapsed = elapsed + seconds
    if elapsed < 1 then return end
    elapsed = 0
    local guid, name = UnitGUID("player"), UnitName("player")
    if not guid or not name then self:Hide() return end
    local bindings = key("MOVEFORWARD") .. " " .. key("MOVEBACKWARD") .. " " .. key("STRAFELEFT") .. " " .. key("STRAFERIGHT")
    if character ~= guid or fingerprint ~= bindings then
        character, fingerprint = guid, bindings
        marker = string.format("%04x%04x%04x%04x", math.random(0,65535), math.random(0,65535),
                               math.random(0,65535), math.random(0,65535))
        paint(marker)
    end
    self:SetScale(1 / UIParent:GetEffectiveScale())
    self:Show()
    SendAddonMessage("WAI_OBS", marker .. " " .. bindings, "WHISPER", name)
end)
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:SetScript("OnEvent", function(self) elapsed = 1 self:Show() end)
