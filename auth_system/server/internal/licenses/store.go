package licenses

import (
	"context"
	"database/sql"
	"fmt"
	"strconv"
	"strings"
	"time"
)

type Store struct {
	db      *sql.DB
	dialect string
}

func NewStore(db *sql.DB, dialect string) *Store {
	return &Store{
		db:      db,
		dialect: strings.ToLower(strings.TrimSpace(dialect)),
	}
}

func (s *Store) EnsureSchema(ctx context.Context) error {
	if s.dialect == "sqlite" {
		return s.ensureSQLiteSchema(ctx)
	}
	return s.ensureMySQLSchema(ctx)
}

func (s *Store) ensureMySQLSchema(ctx context.Context) error {
	statements := []string{
		`CREATE TABLE IF NOT EXISTS licenses (
			id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
			name VARCHAR(128) NOT NULL,
			product_name VARCHAR(255) NOT NULL,
			version_code VARCHAR(16) NOT NULL DEFAULT '099',
			append_version_tail TINYINT(1) NOT NULL DEFAULT 1,
			disclaimer TEXT NOT NULL,
			bound_ip VARCHAR(45) NOT NULL,
			server_ip VARCHAR(45) NOT NULL DEFAULT '',
			bound_qq VARCHAR(64) NOT NULL,
			param_1 TINYINT(1) NOT NULL DEFAULT 0,
			param_2 TINYINT(1) NOT NULL DEFAULT 1,
			param_3 TINYINT(1) NOT NULL DEFAULT 1,
			param_4 TINYINT(1) NOT NULL DEFAULT 1,
			added_at DATETIME NOT NULL,
			expires_at DATETIME NOT NULL,
			active TINYINT(1) NOT NULL DEFAULT 1,
			notes TEXT NOT NULL,
			last_seen_ip VARCHAR(45) NULL,
			last_seen_at DATETIME NULL,
			created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
			updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
			INDEX idx_licenses_ip (bound_ip),
			INDEX idx_licenses_active (active),
			INDEX idx_licenses_name (name)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci`,
		`CREATE TABLE IF NOT EXISTS activation_logs (
			id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
			license_id BIGINT NULL,
			remote_ip VARCHAR(45) NOT NULL,
			success TINYINT(1) NOT NULL,
			reason VARCHAR(255) NOT NULL,
			created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
			INDEX idx_activation_logs_license_id (license_id),
			INDEX idx_activation_logs_created_at (created_at)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci`,
		`CREATE TABLE IF NOT EXISTS app_settings (
			setting_key VARCHAR(128) NOT NULL PRIMARY KEY,
			setting_value TEXT NOT NULL,
			updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
		) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci`,
	}

	for _, stmt := range statements {
		if _, err := s.db.ExecContext(ctx, stmt); err != nil {
			return fmt.Errorf("exec schema: %w", err)
		}
	}
	return s.ensureMySQLLicenseColumns(ctx)
}

func (s *Store) ensureSQLiteSchema(ctx context.Context) error {
	statements := []string{
		`CREATE TABLE IF NOT EXISTS licenses (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			name TEXT NOT NULL,
			product_name TEXT NOT NULL,
			version_code TEXT NOT NULL DEFAULT '099',
			append_version_tail INTEGER NOT NULL DEFAULT 1,
			disclaimer TEXT NOT NULL,
			bound_ip TEXT NOT NULL,
			server_ip TEXT NOT NULL DEFAULT '',
			bound_qq TEXT NOT NULL,
			param_1 INTEGER NOT NULL DEFAULT 0,
			param_2 INTEGER NOT NULL DEFAULT 1,
			param_3 INTEGER NOT NULL DEFAULT 1,
			param_4 INTEGER NOT NULL DEFAULT 1,
			added_at TEXT NOT NULL,
			expires_at TEXT NOT NULL,
			active INTEGER NOT NULL DEFAULT 1,
			notes TEXT NOT NULL DEFAULT '',
			last_seen_ip TEXT NULL,
			last_seen_at TEXT NULL,
			created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
			updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
		)`,
		`CREATE INDEX IF NOT EXISTS idx_licenses_ip ON licenses (bound_ip)`,
		`CREATE INDEX IF NOT EXISTS idx_licenses_active ON licenses (active)`,
		`CREATE INDEX IF NOT EXISTS idx_licenses_name ON licenses (name)`,
		`CREATE TABLE IF NOT EXISTS activation_logs (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			license_id INTEGER NULL,
			remote_ip TEXT NOT NULL,
			success INTEGER NOT NULL,
			reason TEXT NOT NULL,
			created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
		)`,
		`CREATE INDEX IF NOT EXISTS idx_activation_logs_license_id ON activation_logs (license_id)`,
		`CREATE INDEX IF NOT EXISTS idx_activation_logs_created_at ON activation_logs (created_at)`,
		`CREATE TABLE IF NOT EXISTS app_settings (
			setting_key TEXT NOT NULL PRIMARY KEY,
			setting_value TEXT NOT NULL,
			updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
		)`,
	}

	for _, stmt := range statements {
		if _, err := s.db.ExecContext(ctx, stmt); err != nil {
			return fmt.Errorf("exec schema: %w", err)
		}
	}
	return s.ensureSQLiteLicenseColumns(ctx)
}

func (s *Store) ensureMySQLLicenseColumns(ctx context.Context) error {
	columns := []struct {
		name       string
		definition string
	}{
		{name: "version_code", definition: "VARCHAR(16) NOT NULL DEFAULT '099'"},
		{name: "append_version_tail", definition: "TINYINT(1) NOT NULL DEFAULT 1"},
		{name: "server_ip", definition: "VARCHAR(45) NOT NULL DEFAULT ''"},
		{name: "param_1", definition: "TINYINT(1) NOT NULL DEFAULT 0"},
		{name: "param_2", definition: "TINYINT(1) NOT NULL DEFAULT 1"},
		{name: "param_3", definition: "TINYINT(1) NOT NULL DEFAULT 1"},
		{name: "param_4", definition: "TINYINT(1) NOT NULL DEFAULT 1"},
	}

	for _, column := range columns {
		if err := s.ensureMySQLColumn(ctx, "licenses", column.name, column.definition); err != nil {
			return err
		}
	}
	return nil
}

func (s *Store) ensureSQLiteLicenseColumns(ctx context.Context) error {
	columns := []struct {
		name       string
		definition string
	}{
		{name: "version_code", definition: "TEXT NOT NULL DEFAULT '099'"},
		{name: "append_version_tail", definition: "INTEGER NOT NULL DEFAULT 1"},
		{name: "server_ip", definition: "TEXT NOT NULL DEFAULT ''"},
		{name: "param_1", definition: "INTEGER NOT NULL DEFAULT 0"},
		{name: "param_2", definition: "INTEGER NOT NULL DEFAULT 1"},
		{name: "param_3", definition: "INTEGER NOT NULL DEFAULT 1"},
		{name: "param_4", definition: "INTEGER NOT NULL DEFAULT 1"},
	}

	for _, column := range columns {
		if err := s.ensureSQLiteColumn(ctx, "licenses", column.name, column.definition); err != nil {
			return err
		}
	}
	return nil
}

func (s *Store) ensureMySQLColumn(ctx context.Context, tableName, columnName, definition string) error {
	var count int
	err := s.db.QueryRowContext(ctx, `SELECT COUNT(*)
		FROM INFORMATION_SCHEMA.COLUMNS
		WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? AND COLUMN_NAME = ?`,
		tableName,
		columnName,
	).Scan(&count)
	if err != nil {
		return fmt.Errorf("check mysql column %s.%s: %w", tableName, columnName, err)
	}
	if count > 0 {
		return nil
	}

	query := fmt.Sprintf("ALTER TABLE %s ADD COLUMN %s %s", tableName, columnName, definition)
	if _, err := s.db.ExecContext(ctx, query); err != nil {
		return fmt.Errorf("add mysql column %s.%s: %w", tableName, columnName, err)
	}
	return nil
}

func (s *Store) ensureSQLiteColumn(ctx context.Context, tableName, columnName, definition string) error {
	exists, err := s.hasSQLiteColumn(ctx, tableName, columnName)
	if err != nil {
		return err
	}
	if exists {
		return nil
	}

	query := fmt.Sprintf("ALTER TABLE %s ADD COLUMN %s %s", tableName, columnName, definition)
	if _, err := s.db.ExecContext(ctx, query); err != nil {
		return fmt.Errorf("add sqlite column %s.%s: %w", tableName, columnName, err)
	}
	return nil
}

func (s *Store) hasSQLiteColumn(ctx context.Context, tableName, columnName string) (bool, error) {
	rows, err := s.db.QueryContext(ctx, fmt.Sprintf("PRAGMA table_info(%s)", tableName))
	if err != nil {
		return false, fmt.Errorf("check sqlite columns for %s: %w", tableName, err)
	}
	defer rows.Close()

	for rows.Next() {
		var (
			cid        int
			name       string
			dataType   string
			notNull    int
			defaultVal sql.NullString
			pk         int
		)
		if err := rows.Scan(&cid, &name, &dataType, &notNull, &defaultVal, &pk); err != nil {
			return false, fmt.Errorf("scan sqlite column %s: %w", tableName, err)
		}
		if strings.EqualFold(name, columnName) {
			return true, nil
		}
	}

	if err := rows.Err(); err != nil {
		return false, fmt.Errorf("iterate sqlite columns for %s: %w", tableName, err)
	}
	return false, nil
}

func (s *Store) List(ctx context.Context, search string) ([]License, error) {
	search = strings.TrimSpace(search)
	base := `SELECT id, name, product_name, version_code, append_version_tail, disclaimer, bound_ip, server_ip, bound_qq, param_1, param_2, param_3, param_4,
		added_at, expires_at, active, notes, last_seen_ip, last_seen_at, created_at, updated_at
		FROM licenses`

	args := []any{}
	if search != "" {
		base += ` WHERE name LIKE ? OR bound_ip LIKE ? OR server_ip LIKE ? OR bound_qq LIKE ? OR notes LIKE ?`
		pattern := "%" + search + "%"
		args = append(args, pattern, pattern, pattern, pattern, pattern)
	}

	base += ` ORDER BY id DESC`
	rows, err := s.db.QueryContext(ctx, base, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var items []License
	for rows.Next() {
		item, err := scanLicense(rows)
		if err != nil {
			return nil, err
		}
		items = append(items, item)
	}
	return items, rows.Err()
}

func (s *Store) Get(ctx context.Context, id int64) (License, error) {
	row := s.db.QueryRowContext(ctx, `SELECT id, name, product_name, version_code, append_version_tail, disclaimer, bound_ip, server_ip, bound_qq, param_1, param_2, param_3, param_4,
		added_at, expires_at, active, notes, last_seen_ip, last_seen_at, created_at, updated_at
		FROM licenses WHERE id = ?`, id)
	return scanLicense(row)
}

func (s *Store) GetActiveByIP(ctx context.Context, ip string) (License, error) {
	row := s.db.QueryRowContext(ctx, `SELECT id, name, product_name, version_code, append_version_tail, disclaimer, bound_ip, server_ip, bound_qq, param_1, param_2, param_3, param_4,
		added_at, expires_at, active, notes, last_seen_ip, last_seen_at, created_at, updated_at
		FROM licenses
		WHERE bound_ip = ? AND active = 1
		ORDER BY id DESC
		LIMIT 1`, strings.TrimSpace(ip))
	return scanLicense(row)
}

func (s *Store) Create(ctx context.Context, input UpsertInput) (int64, error) {
	if err := input.Normalize(); err != nil {
		return 0, err
	}
	if err := s.ensureBoundIPAvailable(ctx, input.BoundIP, nil); err != nil {
		return 0, err
	}

	result, err := s.db.ExecContext(ctx, `INSERT INTO licenses
		(name, product_name, version_code, append_version_tail, disclaimer, bound_ip, server_ip, bound_qq, param_1, param_2, param_3, param_4, added_at, expires_at, active, notes)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
		input.Name,
		input.ProductName,
		input.VersionCode,
		input.AppendVersionTail,
		input.Disclaimer,
		input.BoundIP,
		input.ServerIP,
		input.BoundQQ,
		input.Param1,
		input.Param2,
		input.Param3,
		input.Param4,
		formatDBTime(input.AddedAt),
		formatDBTime(input.ExpiresAt),
		input.Active,
		input.Notes,
	)
	if err != nil {
		return 0, err
	}
	return result.LastInsertId()
}

func (s *Store) Update(ctx context.Context, id int64, input UpsertInput) error {
	if err := input.Normalize(); err != nil {
		return err
	}
	if err := s.ensureBoundIPAvailable(ctx, input.BoundIP, &id); err != nil {
		return err
	}

	_, err := s.db.ExecContext(ctx, `UPDATE licenses
		SET name = ?, product_name = ?, version_code = ?, append_version_tail = ?, disclaimer = ?, bound_ip = ?, server_ip = ?, bound_qq = ?, param_1 = ?, param_2 = ?, param_3 = ?, param_4 = ?, added_at = ?,
			expires_at = ?, active = ?, notes = ?, updated_at = ?
		WHERE id = ?`,
		input.Name,
		input.ProductName,
		input.VersionCode,
		input.AppendVersionTail,
		input.Disclaimer,
		input.BoundIP,
		input.ServerIP,
		input.BoundQQ,
		input.Param1,
		input.Param2,
		input.Param3,
		input.Param4,
		formatDBTime(input.AddedAt),
		formatDBTime(input.ExpiresAt),
		input.Active,
		input.Notes,
		formatDBTime(time.Now()),
		id,
	)
	return err
}

func (s *Store) Delete(ctx context.Context, id int64) error {
	_, err := s.db.ExecContext(ctx, `DELETE FROM licenses WHERE id = ?`, id)
	return err
}

func (s *Store) DeleteBatch(ctx context.Context, ids []int64) error {
	if len(ids) == 0 {
		return nil
	}

	placeholders := make([]string, 0, len(ids))
	args := make([]any, 0, len(ids))
	for _, id := range ids {
		if id <= 0 {
			continue
		}
		placeholders = append(placeholders, "?")
		args = append(args, id)
	}

	if len(placeholders) == 0 {
		return fmt.Errorf("批量删除的授权 ID 无效")
	}

	query := `DELETE FROM licenses WHERE id IN (` + strings.Join(placeholders, ",") + `)`
	if _, err := s.db.ExecContext(ctx, query, args...); err != nil {
		return fmt.Errorf("delete batch licenses (%s): %w", joinIDs(ids), err)
	}
	return nil
}

func joinIDs(ids []int64) string {
	parts := make([]string, 0, len(ids))
	for _, id := range ids {
		parts = append(parts, strconv.FormatInt(id, 10))
	}
	return strings.Join(parts, ",")
}

func (s *Store) RecordActivation(ctx context.Context, licenseID *int64, remoteIP string, success bool, reason string) error {
	var nullableID any
	if licenseID != nil {
		nullableID = *licenseID
	}

	_, err := s.db.ExecContext(ctx, `INSERT INTO activation_logs
		(license_id, remote_ip, success, reason)
		VALUES (?, ?, ?, ?)`,
		nullableID,
		strings.TrimSpace(remoteIP),
		success,
		strings.TrimSpace(reason),
	)
	return err
}

func (s *Store) MarkSuccessfulActivation(ctx context.Context, licenseID int64, remoteIP string, when time.Time) error {
	_, err := s.db.ExecContext(ctx, `UPDATE licenses
		SET last_seen_ip = ?, last_seen_at = ?, updated_at = ?
		WHERE id = ?`,
		strings.TrimSpace(remoteIP),
		formatDBTime(when),
		formatDBTime(when),
		licenseID,
	)
	return err
}

type scanner interface {
	Scan(dest ...any) error
}

func (s *Store) ensureBoundIPAvailable(ctx context.Context, boundIP string, currentID *int64) error {
	boundIP = strings.TrimSpace(boundIP)
	if boundIP == "" {
		return nil
	}

	var existing License
	var err error
	if currentID != nil {
		existing, err = s.Get(ctx, *currentID)
		if err != nil {
			return err
		}
		if strings.EqualFold(strings.TrimSpace(existing.BoundIP), boundIP) {
			return nil
		}
	}

	var duplicateID int64
	var duplicateName string
	err = s.db.QueryRowContext(ctx, `SELECT id, name FROM licenses WHERE bound_ip = ? ORDER BY id DESC LIMIT 1`, boundIP).Scan(&duplicateID, &duplicateName)
	if err == nil {
		return ValidationError{Message: fmt.Sprintf("绑定 IP 已存在，不能重复添加：%s（用户：%s）", boundIP, strings.TrimSpace(duplicateName))}
	}
	if err == sql.ErrNoRows {
		return nil
	}
	return fmt.Errorf("check duplicate bound ip %s: %w", boundIP, err)
}

func scanLicense(s scanner) (License, error) {
	var item License
	var appendVersionTail any
	var param1 any
	var param2 any
	var param3 any
	var param4 any
	var active any
	var lastSeenIP sql.NullString
	var addedAt any
	var expiresAt any
	var lastSeenAt any
	var createdAt any
	var updatedAt any

	err := s.Scan(
		&item.ID,
		&item.Name,
		&item.ProductName,
		&item.VersionCode,
		&appendVersionTail,
		&item.Disclaimer,
		&item.BoundIP,
		&item.ServerIP,
		&item.BoundQQ,
		&param1,
		&param2,
		&param3,
		&param4,
		&addedAt,
		&expiresAt,
		&active,
		&item.Notes,
		&lastSeenIP,
		&lastSeenAt,
		&createdAt,
		&updatedAt,
	)
	if err != nil {
		return License{}, err
	}

	item.AddedAt, err = parseDBTime(addedAt)
	if err != nil {
		return License{}, err
	}
	item.ExpiresAt, err = parseDBTime(expiresAt)
	if err != nil {
		return License{}, err
	}
	item.CreatedAt, err = parseDBTime(createdAt)
	if err != nil {
		return License{}, err
	}
	item.UpdatedAt, err = parseDBTime(updatedAt)
	if err != nil {
		return License{}, err
	}

	item.Active = parseDBBool(active)
	item.AppendVersionTail = parseDBBool(appendVersionTail)
	if strings.TrimSpace(item.ServerIP) == "" {
		item.ServerIP = item.BoundIP
	}
	item.Param1 = parseDBBool(param1)
	item.Param2 = parseDBBool(param2)
	item.Param3 = parseDBBool(param3)
	item.Param4 = parseDBBool(param4)
	if lastSeenIP.Valid {
		item.LastSeenIP = lastSeenIP.String
	}
	if lastSeenAt != nil {
		value, err := parseDBTime(lastSeenAt)
		if err != nil {
			return License{}, err
		}
		item.LastSeenAt = &value
	}

	return item, nil
}

func formatDBTime(t time.Time) string {
	if t.IsZero() {
		return ""
	}
	return t.Format("2006-01-02 15:04:05")
}

func parseDBTime(raw any) (time.Time, error) {
	switch value := raw.(type) {
	case nil:
		return time.Time{}, nil
	case time.Time:
		return value, nil
	case []byte:
		return parseDBTimeString(string(value))
	case string:
		return parseDBTimeString(value)
	default:
		return time.Time{}, fmt.Errorf("unsupported time value type %T", raw)
	}
}

func parseDBTimeString(raw string) (time.Time, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return time.Time{}, nil
	}

	layouts := []string{
		"2006-01-02 15:04:05",
		"2006-01-02T15:04:05",
		"2006-01-02T15:04:05Z07:00",
		time.RFC3339,
		time.RFC3339Nano,
	}

	var lastErr error
	for _, layout := range layouts {
		t, err := time.ParseInLocation(layout, raw, time.Local)
		if err == nil {
			return t, nil
		}
		lastErr = err
	}

	return time.Time{}, fmt.Errorf("parse db time %q: %w", raw, lastErr)
}

func parseDBBool(raw any) bool {
	switch value := raw.(type) {
	case bool:
		return value
	case int64:
		return value != 0
	case int32:
		return value != 0
	case int:
		return value != 0
	case []byte:
		return parseDBBoolString(string(value))
	case string:
		return parseDBBoolString(value)
	default:
		return false
	}
}

func parseDBBoolString(raw string) bool {
	raw = strings.TrimSpace(strings.ToLower(raw))
	return raw == "1" || raw == "true" || raw == "yes" || raw == "on"
}
