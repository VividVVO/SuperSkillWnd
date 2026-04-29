package config

import (
	"path/filepath"
	"testing"
)

func TestResolveSQLitePathUsesEnvBaseDir(t *testing.T) {
	t.Setenv(EnvBaseDirKey, filepath.Join("G:", "deploy", "authserver"))

	got := resolveSQLitePath(filepath.Join(".", "data", "auth_system.db"))
	want := filepath.Clean(filepath.Join("G:", "deploy", "authserver", "data", "auth_system.db"))
	if got != want {
		t.Fatalf("resolveSQLitePath() = %q, want %q", got, want)
	}
}

func TestResolveSQLitePathKeepsAbsolutePath(t *testing.T) {
	t.Setenv(EnvBaseDirKey, filepath.Join("G:", "deploy", "authserver"))

	absolute := filepath.Clean(`D:\shared\auth_system.db`)
	got := resolveSQLitePath(absolute)
	if got != absolute {
		t.Fatalf("resolveSQLitePath() = %q, want %q", got, absolute)
	}
}
