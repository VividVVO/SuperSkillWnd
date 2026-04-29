package licenses

import (
	"errors"
	"fmt"
	"strings"
	"time"
)

type ValidationError struct {
	Message string
}

func (e ValidationError) Error() string {
	return e.Message
}

func IsValidationError(err error) bool {
	var target ValidationError
	return errors.As(err, &target)
}

type License struct {
	ID                int64
	Name              string
	ProductName       string
	VersionCode       string
	AppendVersionTail bool
	Disclaimer        string
	BoundIP           string
	ServerIP          string
	BoundQQ           string
	Param1            bool
	Param2            bool
	Param3            bool
	Param4            bool
	AddedAt           time.Time
	ExpiresAt         time.Time
	Active            bool
	Notes             string
	LastSeenIP        string
	LastSeenAt        *time.Time
	CreatedAt         time.Time
	UpdatedAt         time.Time
}

type UpsertInput struct {
	Name              string
	ProductName       string
	VersionCode       string
	AppendVersionTail bool
	Disclaimer        string
	BoundIP           string
	ServerIP          string
	BoundQQ           string
	Param1            bool
	Param2            bool
	Param3            bool
	Param4            bool
	AddedAt           time.Time
	ExpiresAt         time.Time
	Active            bool
	Notes             string
	DurationValue     int
	DurationUnit      string
}

func (in *UpsertInput) Normalize() error {
	in.Name = strings.TrimSpace(in.Name)
	in.ProductName = strings.TrimSpace(in.ProductName)
	in.VersionCode = strings.TrimSpace(in.VersionCode)
	in.Disclaimer = strings.TrimSpace(in.Disclaimer)
	in.BoundIP = strings.TrimSpace(in.BoundIP)
	in.ServerIP = strings.TrimSpace(in.ServerIP)
	in.BoundQQ = strings.TrimSpace(in.BoundQQ)
	in.Notes = strings.TrimSpace(in.Notes)
	in.DurationUnit = strings.TrimSpace(strings.ToLower(in.DurationUnit))
	if in.VersionCode == "" {
		in.VersionCode = "099"
	}

	if in.Name == "" {
		return ValidationError{Message: "用户名不能为空"}
	}
	if in.ProductName == "" {
		return ValidationError{Message: "产品名不能为空"}
	}
	if in.Disclaimer == "" {
		return ValidationError{Message: "提示语不能为空"}
	}
	if in.BoundIP == "" {
		return ValidationError{Message: "绑定 IP 不能为空"}
	}
	if in.BoundQQ == "" {
		return ValidationError{Message: "绑定 QQ 不能为空"}
	}
	if in.ServerIP == "" {
		in.ServerIP = in.BoundIP
	}
	if in.AddedAt.IsZero() {
		return ValidationError{Message: "添加时间不能为空"}
	}

	if in.DurationValue > 0 {
		expiresAt, err := ApplyDuration(in.AddedAt, in.DurationValue, in.DurationUnit)
		if err != nil {
			return err
		}
		in.ExpiresAt = expiresAt
	}

	if in.ExpiresAt.IsZero() {
		return ValidationError{Message: "到期时间不能为空"}
	}
	if !in.ExpiresAt.After(in.AddedAt) {
		return ValidationError{Message: "到期时间必须晚于添加时间"}
	}

	return nil
}

func ApplyDuration(start time.Time, value int, unit string) (time.Time, error) {
	if value <= 0 {
		return time.Time{}, ValidationError{Message: "时长必须大于 0"}
	}

	switch strings.ToLower(strings.TrimSpace(unit)) {
	case "day":
		return start.AddDate(0, 0, value), nil
	case "week":
		return start.AddDate(0, 0, value*7), nil
	case "month":
		return start.AddDate(0, value, 0), nil
	case "year":
		return start.AddDate(value, 0, 0), nil
	default:
		return time.Time{}, ValidationError{Message: fmt.Sprintf("不支持的时长单位: %s", unit)}
	}
}

func ParseFormDateTime(raw string) (time.Time, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return time.Time{}, nil
	}

	layouts := []string{
		"2006-01-02T15:04",
		"2006-01-02T15:04:05",
		"2006-01-02 15:04:05",
		time.RFC3339,
	}

	var lastErr error
	for _, layout := range layouts {
		t, err := time.ParseInLocation(layout, raw, time.Local)
		if err == nil {
			return t, nil
		}
		lastErr = err
	}

	return time.Time{}, fmt.Errorf("解析时间 %q 失败: %w", raw, lastErr)
}
