package config

import (
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

const EnvBaseDirKey = "AUTHSERVER_ENV_DIR"

type Config struct {
	HTTPListenAddr     string
	MySQLDSN           string
	SQLitePath         string
	AdminAllowPublic   bool
	AdminAllowedNets   []*net.IPNet
	AdminUsername      string
	AdminPassword      string
	AdminSessionSecret string
	ProductName        string
	Disclaimer         string
	BlobKey            string
	ProxyHeader        string
	DefaultDuration    int
	DefaultUnit        string
	DefaultParam1      bool
	DefaultParam2      bool
	DefaultParam3      bool
	DefaultParam4      bool
}

func Load() (Config, error) {
	cfg := Config{
		HTTPListenAddr:     envString("HTTP_LISTEN_ADDR", ":8080"),
		MySQLDSN:           strings.TrimSpace(os.Getenv("MYSQL_DSN")),
		SQLitePath:         resolveSQLitePath(envString("SQLITE_PATH", filepath.Join(".", "data", "auth_system.db"))),
		AdminAllowPublic:   envBool("ADMIN_ALLOW_PUBLIC", false),
		AdminUsername:      envString("ADMIN_USERNAME", "admin"),
		AdminPassword:      envString("ADMIN_PASSWORD", "change-me"),
		AdminSessionSecret: envString("ADMIN_SESSION_SECRET", "change-me-session-secret"),
		ProductName:        envString("PRODUCT_NAME", "099冒险岛-自定义属性"),
		Disclaimer:         envString("DISCLAIMER", "本程序仅做学习交流之用，不得用于商业用途！如作他用所承受的法律责任一概与作者无关（下载使用即代表你同意上述观点）"),
		BlobKey:            envString("BLOB_KEY", "heifengye111"),
		ProxyHeader:        strings.TrimSpace(os.Getenv("PROXY_HEADER")),
		DefaultDuration:    envInt("DEFAULT_DURATION_VALUE", 1),
		DefaultUnit:        envString("DEFAULT_DURATION_UNIT", "month"),
		DefaultParam1:      envBool("DEFAULT_PARAM1", false),
		DefaultParam2:      envBool("DEFAULT_PARAM2", true),
		DefaultParam3:      envBool("DEFAULT_PARAM3", true),
		DefaultParam4:      envBool("DEFAULT_PARAM4", true),
	}

	cidrs := envString("ADMIN_ALLOWED_CIDRS", strings.Join(defaultCIDRs(), ","))
	nets, err := parseCIDRs(cidrs)
	if err != nil {
		return Config{}, err
	}
	cfg.AdminAllowedNets = nets

	return cfg, nil
}

func (c Config) Database() (driverName string, dataSource string, err error) {
	if strings.TrimSpace(c.MySQLDSN) != "" {
		return "mysql", c.MySQLDSN, nil
	}

	sqlitePath := strings.TrimSpace(c.SQLitePath)
	if sqlitePath == "" {
		return "", "", fmt.Errorf("SQLITE_PATH 不能为空")
	}

	return "sqlite", sqlitePath, nil
}

func defaultCIDRs() []string {
	return []string{
		"127.0.0.0/8",
		"::1/128",
		"10.0.0.0/8",
		"172.16.0.0/12",
		"192.168.0.0/16",
		"fc00::/7",
	}
}

func parseCIDRs(raw string) ([]*net.IPNet, error) {
	parts := strings.Split(raw, ",")
	nets := make([]*net.IPNet, 0, len(parts))
	for _, part := range parts {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		_, network, err := net.ParseCIDR(part)
		if err != nil {
			return nil, fmt.Errorf("parse cidr %q: %w", part, err)
		}
		nets = append(nets, network)
	}
	return nets, nil
}

func resolveSQLitePath(raw string) string {
	raw = strings.TrimSpace(raw)
	if raw == "" || filepath.IsAbs(raw) {
		return raw
	}

	baseDir := strings.TrimSpace(os.Getenv(EnvBaseDirKey))
	if baseDir == "" {
		return filepath.Clean(raw)
	}

	return filepath.Clean(filepath.Join(baseDir, raw))
}

func envString(key, fallback string) string {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	return value
}

func envBool(key string, fallback bool) bool {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.ParseBool(value)
	if err != nil {
		return fallback
	}
	return parsed
}

func envInt(key string, fallback int) int {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.Atoi(value)
	if err != nil {
		return fallback
	}
	return parsed
}
