package licenses

import (
	"context"
	"database/sql"
	"testing"
	"time"

	_ "modernc.org/sqlite"
)

func TestStoreCreateRejectsDuplicateBoundIP(t *testing.T) {
	store := newTestStore(t)
	ctx := context.Background()

	if _, err := store.Create(ctx, sampleUpsertInput("10.0.0.1", "user-a")); err != nil {
		t.Fatalf("first create failed: %v", err)
	}

	_, err := store.Create(ctx, sampleUpsertInput("10.0.0.1", "user-b"))
	if err == nil {
		t.Fatalf("expected duplicate ip validation error")
	}
	if !IsValidationError(err) {
		t.Fatalf("expected validation error, got %T: %v", err, err)
	}
}

func TestStoreUpdateRejectsDuplicateBoundIPWhenChanged(t *testing.T) {
	store := newTestStore(t)
	ctx := context.Background()

	if _, err := store.Create(ctx, sampleUpsertInput("10.0.0.1", "user-a")); err != nil {
		t.Fatalf("first create failed: %v", err)
	}
	secondID, err := store.Create(ctx, sampleUpsertInput("10.0.0.2", "user-b"))
	if err != nil {
		t.Fatalf("second create failed: %v", err)
	}

	input := sampleUpsertInput("10.0.0.1", "user-b")
	err = store.Update(ctx, secondID, input)
	if err == nil {
		t.Fatalf("expected duplicate ip validation error")
	}
	if !IsValidationError(err) {
		t.Fatalf("expected validation error, got %T: %v", err, err)
	}
}

func TestStorePersistsAppendVersionTail(t *testing.T) {
	store := newTestStore(t)
	ctx := context.Background()

	input := sampleUpsertInput("10.0.0.3", "user-c")
	input.AppendVersionTail = false

	id, err := store.Create(ctx, input)
	if err != nil {
		t.Fatalf("create failed: %v", err)
	}

	item, err := store.Get(ctx, id)
	if err != nil {
		t.Fatalf("get failed: %v", err)
	}
	if item.AppendVersionTail {
		t.Fatalf("expected append_version_tail to persist as false")
	}
}

func TestStoreDefaultsServerIPToBoundIP(t *testing.T) {
	store := newTestStore(t)
	ctx := context.Background()

	id, err := store.Create(ctx, sampleUpsertInput("10.0.0.4", "user-d"))
	if err != nil {
		t.Fatalf("create failed: %v", err)
	}

	item, err := store.Get(ctx, id)
	if err != nil {
		t.Fatalf("get failed: %v", err)
	}
	if item.ServerIP != "10.0.0.4" {
		t.Fatalf("expected server_ip to default to bound_ip, got %q", item.ServerIP)
	}
}

func TestStorePersistsCustomServerIP(t *testing.T) {
	store := newTestStore(t)
	ctx := context.Background()

	input := sampleUpsertInput("10.0.0.5", "user-e")
	input.ServerIP = "20.0.0.5"

	id, err := store.Create(ctx, input)
	if err != nil {
		t.Fatalf("create failed: %v", err)
	}

	item, err := store.Get(ctx, id)
	if err != nil {
		t.Fatalf("get failed: %v", err)
	}
	if item.ServerIP != "20.0.0.5" {
		t.Fatalf("expected custom server_ip to persist, got %q", item.ServerIP)
	}
}

func newTestStore(t *testing.T) *Store {
	t.Helper()

	db, err := sql.Open("sqlite", ":memory:")
	if err != nil {
		t.Fatalf("open sqlite: %v", err)
	}
	t.Cleanup(func() {
		_ = db.Close()
	})

	store := NewStore(db, "sqlite")
	if err := store.EnsureSchema(context.Background()); err != nil {
		t.Fatalf("ensure schema: %v", err)
	}
	return store
}

func sampleUpsertInput(ip, name string) UpsertInput {
	now := time.Date(2026, 4, 25, 18, 0, 0, 0, time.Local)
	return UpsertInput{
		Name:              name,
		ProductName:       "099冒险岛-自定义属性",
		VersionCode:       "099",
		AppendVersionTail: true,
		Disclaimer:        "demo disclaimer",
		BoundIP:           ip,
		BoundQQ:           "123456",
		Param1:            false,
		Param2:            true,
		Param3:            true,
		Param4:            true,
		AddedAt:           now,
		ExpiresAt:         now.AddDate(1, 0, 0),
		Active:            true,
		Notes:             "",
	}
}
