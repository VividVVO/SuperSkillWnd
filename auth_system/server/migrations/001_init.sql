CREATE TABLE IF NOT EXISTS licenses (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    product_name VARCHAR(255) NOT NULL,
    version_code VARCHAR(16) NOT NULL DEFAULT '095',
    append_version_tail TINYINT(1) NOT NULL DEFAULT 1,
    disclaimer TEXT NOT NULL,
    bound_ip VARCHAR(45) NOT NULL,
    server_ip VARCHAR(45) NOT NULL DEFAULT '',
    bound_qq VARCHAR(64) NOT NULL,
    param_1 TINYINT(1) NOT NULL DEFAULT 1,
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
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_licenses_ip (bound_ip),
    INDEX idx_licenses_active (active),
    INDEX idx_licenses_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS activation_logs (
    id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    license_id BIGINT NULL,
    remote_ip VARCHAR(45) NOT NULL,
    success TINYINT(1) NOT NULL,
    reason VARCHAR(255) NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_activation_logs_license_id (license_id),
    INDEX idx_activation_logs_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS app_settings (
    setting_key VARCHAR(128) NOT NULL PRIMARY KEY,
    setting_value TEXT NOT NULL,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
