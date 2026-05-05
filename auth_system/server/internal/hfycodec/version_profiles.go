package hfycodec

import "strings"

const DefaultVersionCode = "095"

type VersionProfile struct {
	Code           string
	Label          string
	ProductName    string
	HasTail        bool
	TailPrefix     string
	SecondIPSuffix string
}

var versionProfiles = []VersionProfile{
	{Code: "079", Label: "079", ProductName: "079", HasTail: true},
	{Code: "083", Label: "083", ProductName: "083", HasTail: true, SecondIPSuffix: " "},
	{Code: "085", Label: "085", ProductName: "085", HasTail: true, SecondIPSuffix: " "},
	{Code: "095", Label: "095", ProductName: "095", HasTail: true, SecondIPSuffix: " "},
	{Code: "099", Label: "099", ProductName: "099", HasTail: true},
}

var versionProfileMap = buildVersionProfileMap(versionProfiles)

func AvailableVersionProfiles() []VersionProfile {
	out := make([]VersionProfile, len(versionProfiles))
	copy(out, versionProfiles)
	return out
}

func NormalizeVersionCode(raw string) string {
	code := strings.TrimSpace(raw)
	if code == "" {
		return DefaultVersionCode
	}
	return code
}

func ResolveVersionProfile(raw string) (VersionProfile, bool) {
	code := NormalizeVersionCode(raw)
	profile, ok := versionProfileMap[code]
	return profile, ok
}

func buildVersionProfileMap(items []VersionProfile) map[string]VersionProfile {
	out := make(map[string]VersionProfile, len(items))
	for _, item := range items {
		out[item.Code] = item
	}
	return out
}
