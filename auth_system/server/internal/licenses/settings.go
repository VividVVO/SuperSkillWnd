package licenses

import (
	"context"
	"database/sql"
	"fmt"
	"strings"
)

const appendVersionTailsSettingKey = "append_version_tails"

type Settings struct {
	AppendVersionTails bool
}

func (s *Store) GetSettings(ctx context.Context) (Settings, error) {
	enabled, err := s.GetAppendVersionTails(ctx)
	if err != nil {
		return Settings{}, err
	}
	return Settings{
		AppendVersionTails: enabled,
	}, nil
}

func (s *Store) GetAppendVersionTails(ctx context.Context) (bool, error) {
	raw, err := s.getSetting(ctx, appendVersionTailsSettingKey)
	if err != nil {
		return false, err
	}
	return parseDBBoolString(raw), nil
}

func (s *Store) SetAppendVersionTails(ctx context.Context, enabled bool) error {
	value := "0"
	if enabled {
		value = "1"
	}
	return s.setSetting(ctx, appendVersionTailsSettingKey, value)
}

func (s *Store) getSetting(ctx context.Context, key string) (string, error) {
	var value string
	err := s.db.QueryRowContext(ctx, `SELECT setting_value FROM app_settings WHERE setting_key = ?`, strings.TrimSpace(key)).Scan(&value)
	if err == nil {
		return value, nil
	}
	if err == sql.ErrNoRows {
		return "", nil
	}
	return "", fmt.Errorf("get setting %s: %w", key, err)
}

func (s *Store) setSetting(ctx context.Context, key, value string) error {
	key = strings.TrimSpace(key)
	value = strings.TrimSpace(value)

	if s.dialect == "sqlite" {
		_, err := s.db.ExecContext(ctx, `INSERT INTO app_settings (setting_key, setting_value)
			VALUES (?, ?)
			ON CONFLICT(setting_key) DO UPDATE SET setting_value = excluded.setting_value, updated_at = CURRENT_TIMESTAMP`,
			key,
			value,
		)
		if err != nil {
			return fmt.Errorf("set sqlite setting %s: %w", key, err)
		}
		return nil
	}

	_, err := s.db.ExecContext(ctx, `INSERT INTO app_settings (setting_key, setting_value)
		VALUES (?, ?)
		ON DUPLICATE KEY UPDATE setting_value = VALUES(setting_value), updated_at = CURRENT_TIMESTAMP`,
		key,
		value,
	)
	if err != nil {
		return fmt.Errorf("set mysql setting %s: %w", key, err)
	}
	return nil
}
