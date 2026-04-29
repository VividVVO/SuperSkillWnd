package main

import (
	"bufio"
	"context"
	"database/sql"
	"errors"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	_ "github.com/go-sql-driver/mysql"
	_ "modernc.org/sqlite"

	"authsystem/server/internal/config"
	"authsystem/server/internal/licenses"
	"authsystem/server/internal/web"
)

func main() {
	if err := loadLocalEnv(); err != nil {
		log.Fatalf("load local env: %v", err)
	}

	cfg, err := config.Load()
	if err != nil {
		log.Fatalf("load config: %v", err)
	}

	driverName, dataSource, err := cfg.Database()
	if err != nil {
		log.Fatalf("database config: %v", err)
	}

	if driverName == "sqlite" {
		dir := filepath.Dir(dataSource)
		if dir != "." && dir != "" {
			if err := os.MkdirAll(dir, 0o755); err != nil {
				log.Fatalf("create sqlite directory: %v", err)
			}
		}
	}

	db, err := sql.Open(driverName, dataSource)
	if err != nil {
		log.Fatalf("open database: %v", err)
	}
	defer db.Close()

	db.SetConnMaxLifetime(5 * time.Minute)
	db.SetMaxOpenConns(10)
	db.SetMaxIdleConns(10)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	if err := db.PingContext(ctx); err != nil {
		log.Fatalf("ping database: %v", err)
	}

	store := licenses.NewStore(db, driverName)
	if err := store.EnsureSchema(ctx); err != nil {
		log.Fatalf("ensure schema: %v", err)
	}

	app, err := web.NewApp(cfg, store)
	if err != nil {
		log.Fatalf("build app: %v", err)
	}

	srv := &http.Server{
		Addr:              cfg.HTTPListenAddr,
		Handler:           app.Handler(),
		ReadHeaderTimeout: 5 * time.Second,
	}

	log.Printf("auth server listening on %s using %s", cfg.HTTPListenAddr, driverName)
	go func() {
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("listen: %v", err)
		}
	}()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer shutdownCancel()

	if err := srv.Shutdown(shutdownCtx); err != nil {
		log.Printf("shutdown: %v", err)
	}
}

func loadLocalEnv() error {
	candidates := make([]string, 0, 2)
	if exePath, err := os.Executable(); err == nil {
		candidates = append(candidates, filepath.Join(filepath.Dir(exePath), ".env"))
	}
	candidates = append(candidates, ".env")

	seen := make(map[string]struct{}, len(candidates))
	for _, candidate := range candidates {
		absPath, err := filepath.Abs(candidate)
		if err == nil {
			candidate = absPath
		}
		if _, ok := seen[candidate]; ok {
			continue
		}
		seen[candidate] = struct{}{}

		if err := loadEnvFile(candidate); err != nil {
			return err
		}
	}

	return nil
}

func loadEnvFile(path string) error {
	file, err := os.Open(path)
	if errors.Is(err, os.ErrNotExist) {
		return nil
	}
	if err != nil {
		return err
	}
	defer file.Close()

	if _, exists := os.LookupEnv(config.EnvBaseDirKey); !exists {
		if absPath, absErr := filepath.Abs(path); absErr == nil {
			_ = os.Setenv(config.EnvBaseDirKey, filepath.Dir(absPath))
		} else {
			_ = os.Setenv(config.EnvBaseDirKey, filepath.Dir(path))
		}
	}

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "export ") {
			line = strings.TrimSpace(strings.TrimPrefix(line, "export "))
		}

		key, value, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}

		key = strings.TrimSpace(key)
		value = strings.TrimSpace(value)
		value = strings.Trim(value, `"'`)
		if key == "" {
			continue
		}
		if _, exists := os.LookupEnv(key); exists {
			continue
		}
		if err := os.Setenv(key, value); err != nil {
			return err
		}
	}

	return scanner.Err()
}
