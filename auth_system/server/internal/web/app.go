package web

import (
	"bytes"
	"crypto/hmac"
	"crypto/sha256"
	"embed"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"authsystem/server/internal/config"
	"authsystem/server/internal/hfycodec"
	"authsystem/server/internal/licenses"
)

//go:embed templates/*.html
var templateFS embed.FS

//go:embed static/*
var staticFS embed.FS

const (
	adminSessionCookieName = "auth_admin_session"
	adminSessionTTL        = 12 * time.Hour
	adminRememberTTL       = 30 * 24 * time.Hour

	activationFailedMessage = "尚未获得授权或已经到期，请联系作者"
	activationOKMessage     = "激活授权成功"
	localLicenseIP          = "183.141.76.29"
)

type App struct {
	cfg               config.Config
	store             *licenses.Store
	adminPageTemplate string
	loginPageTemplate string
	assetVersion      string
}

type adminPageData struct {
	Bootstrap string
}

type adminBootstrap struct {
	Search   string             `json:"search"`
	Items    []adminLicenseItem `json:"items"`
	Summary  adminSummary       `json:"summary"`
	Defaults adminDefaults      `json:"defaults"`
	Settings adminSettings      `json:"settings"`
	Versions []adminVersionItem `json:"versions"`
	Meta     adminMeta          `json:"meta"`
}

type adminListResponse struct {
	Items   []adminLicenseItem `json:"items"`
	Summary adminSummary       `json:"summary"`
}

type adminSummary struct {
	Total    int `json:"total"`
	Active   int `json:"active"`
	Expired  int `json:"expired"`
	Disabled int `json:"disabled"`
}

type adminDefaults struct {
	ProductName       string `json:"product_name"`
	VersionCode       string `json:"version_code"`
	AppendVersionTail bool   `json:"append_version_tail"`
	ServerIP          string `json:"server_ip"`
	Disclaimer        string `json:"disclaimer"`
	DurationValue     int    `json:"duration_value"`
	DurationUnit      string `json:"duration_unit"`
	Param1            bool   `json:"param1"`
	Param2            bool   `json:"param2"`
	Param3            bool   `json:"param3"`
	Param4            bool   `json:"param4"`
	AddedAt           string `json:"added_at"`
	ExpiresAt         string `json:"expires_at"`
}

type adminMeta struct {
	DatabaseMode    string `json:"database_mode"`
	AdminAccessMode string `json:"admin_access_mode"`
	ServerTime      string `json:"server_time"`
}

type adminSettings struct {
	AppendVersionTails bool `json:"append_version_tails"`
}

type adminVersionItem struct {
	Code        string `json:"code"`
	Label       string `json:"label"`
	ProductName string `json:"product_name"`
	HasTail     bool   `json:"has_tail"`
	TailPreview string `json:"tail_preview"`
}

type adminLicenseItem struct {
	ID                int64  `json:"id"`
	Name              string `json:"name"`
	Product           string `json:"product_name"`
	VersionCode       string `json:"version_code"`
	AppendVersionTail bool   `json:"append_version_tail"`
	Disclaimer        string `json:"disclaimer"`
	BoundIP           string `json:"bound_ip"`
	ServerIP          string `json:"server_ip"`
	BoundQQ           string `json:"bound_qq"`
	Param1            bool   `json:"param1"`
	Param2            bool   `json:"param2"`
	Param3            bool   `json:"param3"`
	Param4            bool   `json:"param4"`
	AddedAt           string `json:"added_at"`
	ExpiresAt         string `json:"expires_at"`
	Active            bool   `json:"active"`
	Notes             string `json:"notes"`
	LastSeenIP        string `json:"last_seen_ip"`
	LastSeenAt        string `json:"last_seen_at"`
	CreatedAt         string `json:"created_at"`
	UpdatedAt         string `json:"updated_at"`
}

type adminLicensePayload struct {
	ID                *int64 `json:"id"`
	Name              string `json:"name"`
	ProductName       string `json:"product_name"`
	VersionCode       string `json:"version_code"`
	AppendVersionTail bool   `json:"append_version_tail"`
	Disclaimer        string `json:"disclaimer"`
	BoundIP           string `json:"bound_ip"`
	ServerIP          string `json:"server_ip"`
	BoundQQ           string `json:"bound_qq"`
	Param1            bool   `json:"param1"`
	Param2            bool   `json:"param2"`
	Param3            bool   `json:"param3"`
	Param4            bool   `json:"param4"`
	AddedAt           string `json:"added_at"`
	ExpiresAt         string `json:"expires_at"`
	Active            bool   `json:"active"`
	Notes             string `json:"notes"`
	DurationValue     int    `json:"duration_value"`
	DurationUnit      string `json:"duration_unit"`
}

type adminSettingsPayload struct {
	AppendVersionTails bool `json:"append_version_tails"`
}

type batchDeletePayload struct {
	IDs []int64 `json:"ids"`
}

type apiMessage struct {
	OK      bool   `json:"ok"`
	Message string `json:"message"`
}

type apiDataMessage struct {
	OK      bool   `json:"ok"`
	Data    any    `json:"data,omitempty"`
	Message string `json:"message,omitempty"`
}

type loginPayload struct {
	Username string `json:"username"`
	Password string `json:"password"`
	Remember bool   `json:"remember"`
}

type loginResponse struct {
	OK         bool   `json:"ok"`
	Message    string `json:"message"`
	RedirectTo string `json:"redirect_to,omitempty"`
}

type activateRequest struct {
	Internal   bool   `json:"internal"`
	ClientTime string `json:"client_time"`
}

type activateResponse struct {
	OK         bool   `json:"ok"`
	Message    string `json:"message"`
	FixedText  string `json:"fixed_text,omitempty"`
	BlobBase64 string `json:"blob_base64,omitempty"`
	ServerTime string `json:"server_time,omitempty"`
	ExpiresAt  string `json:"expires_at,omitempty"`
	BoundIP    string `json:"bound_ip,omitempty"`
}

func NewApp(cfg config.Config, store *licenses.Store) (*App, error) {
	pageBytes, err := templateFS.ReadFile("templates/license_list.html")
	if err != nil {
		return nil, err
	}
	loginBytes, err := templateFS.ReadFile("templates/login.html")
	if err != nil {
		return nil, err
	}

	app := &App{
		cfg:               cfg,
		store:             store,
		adminPageTemplate: string(pageBytes),
		loginPageTemplate: string(loginBytes),
		assetVersion:      strconv.FormatInt(time.Now().Unix(), 10),
	}

	return app, nil
}

func (a *App) Handler() http.Handler {
	mux := http.NewServeMux()
	staticRoot, err := fs.Sub(staticFS, "static")
	if err != nil {
		panic(fmt.Sprintf("build static fs failed: %v", err))
	}
	staticHandler := http.StripPrefix("/static/", http.FileServer(http.FS(staticRoot)))

	mux.Handle("/static/", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
		w.Header().Set("Pragma", "no-cache")
		w.Header().Set("Expires", "0")
		staticHandler.ServeHTTP(w, r)
	}))
	mux.HandleFunc("/healthz", a.handleHealth)
	mux.HandleFunc("/api/v1/ip", a.handleIP)
	mux.HandleFunc("/api/v1/activate", a.handleActivate)

	mux.Handle("/login", a.adminEntryGuard(http.HandlerFunc(a.handleLoginPage)))
	mux.Handle("/admin/api/login", a.adminEntryGuard(http.HandlerFunc(a.handleAdminAPILogin)))
	mux.Handle("/logout", a.adminEntryGuard(http.HandlerFunc(a.handleLogout)))
	mux.Handle("/admin/api/licenses", a.adminGuard(http.HandlerFunc(a.handleAdminAPILicenses)))
	mux.Handle("/admin/api/licenses/", a.adminGuard(http.HandlerFunc(a.handleAdminAPILicenseByID)))
	mux.Handle("/admin/licenses", a.adminGuard(http.HandlerFunc(a.handleAdminPage)))
	mux.Handle("/admin/licenses/new", a.adminGuard(http.HandlerFunc(a.handleAdminLegacyRedirect)))
	mux.Handle("/admin/licenses/", a.adminGuard(http.HandlerFunc(a.handleAdminLegacyRedirect)))
	mux.Handle("/", a.adminEntryGuard(http.HandlerFunc(a.handleRoot)))

	return loggingMiddleware(mux)
}

func (a *App) handleRoot(w http.ResponseWriter, r *http.Request) {
	if a.isAuthenticated(r) {
		http.Redirect(w, r, "/admin/licenses", http.StatusFound)
		return
	}
	http.Redirect(w, r, "/login", http.StatusFound)
}

func (a *App) handleLoginPage(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if a.isAuthenticated(r) {
		http.Redirect(w, r, a.safeNextPath(r.URL.Query().Get("next")), http.StatusFound)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_, _ = w.Write([]byte(a.loginPageTemplate))
}

func (a *App) handleAdminAPILogin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	defer r.Body.Close()
	rawBody, err := io.ReadAll(r.Body)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, loginResponse{
			OK:      false,
			Message: "读取登录请求失败",
		})
		return
	}
	rawBody = bytes.TrimPrefix(rawBody, []byte{0xEF, 0xBB, 0xBF})

	var payload loginPayload
	decoder := json.NewDecoder(bytes.NewReader(rawBody))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&payload); err != nil {
		writeJSON(w, http.StatusBadRequest, loginResponse{
			OK:      false,
			Message: "登录请求格式错误",
		})
		return
	}

	if strings.TrimSpace(payload.Username) != a.cfg.AdminUsername || payload.Password != a.cfg.AdminPassword {
		writeJSON(w, http.StatusUnauthorized, loginResponse{
			OK:      false,
			Message: "账号或密码错误",
		})
		return
	}

	a.setSessionCookie(w, r, payload.Remember)
	redirectTo := a.safeNextPath(r.URL.Query().Get("next"))
	writeJSON(w, http.StatusOK, loginResponse{
		OK:         true,
		Message:    "登录成功",
		RedirectTo: redirectTo,
	})
}

func (a *App) handleLogout(w http.ResponseWriter, r *http.Request) {
	a.clearSessionCookie(w, r)
	http.Redirect(w, r, "/login", http.StatusFound)
}

func (a *App) handleHealth(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":   true,
		"time": time.Now().Format(time.RFC3339),
	})
}

func (a *App) handleIP(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{
		"ip":          a.licenseClientIP(r),
		"server_time": time.Now().Format(time.RFC3339),
	})
}

func (a *App) handleActivate(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeJSON(w, http.StatusMethodNotAllowed, activateResponse{
			OK:      false,
			Message: activationFailedMessage,
		})
		return
	}

	var req activateRequest
	_ = json.NewDecoder(r.Body).Decode(&req)

	remoteIP := a.licenseClientIP(r)
	now := time.Now()
	licenseItem, err := a.store.GetActiveByIP(r.Context(), remoteIP)
	if err != nil {
		_ = a.store.RecordActivation(r.Context(), nil, remoteIP, false, "license not found")
		writeJSON(w, http.StatusUnauthorized, activateResponse{
			OK:      false,
			Message: activationFailedMessage,
		})
		return
	}

	if !licenseItem.Active || now.After(licenseItem.ExpiresAt) {
		licenseID := licenseItem.ID
		_ = a.store.RecordActivation(r.Context(), &licenseID, remoteIP, false, "license expired or inactive")
		writeJSON(w, http.StatusUnauthorized, activateResponse{
			OK:      false,
			Message: activationFailedMessage,
		})
		return
	}

	versionProfile, ok := hfycodec.ResolveVersionProfile(licenseItem.VersionCode)
	if !ok {
		licenseID := licenseItem.ID
		_ = a.store.RecordActivation(r.Context(), &licenseID, remoteIP, false, "unsupported license version")
		writeJSON(w, http.StatusInternalServerError, activateResponse{
			OK:      false,
			Message: "授权版本配置错误，请联系管理员",
		})
		return
	}

	err = nil
	if err != nil {
		licenseID := licenseItem.ID
		_ = a.store.RecordActivation(r.Context(), &licenseID, remoteIP, false, "load settings failed")
		writeJSON(w, http.StatusInternalServerError, activateResponse{
			OK:      false,
			Message: "授权配置读取失败，请联系管理员",
		})
		return
	}

	fixedText := hfycodec.FormatFixedText(
		licenseItem.ProductName,
		licenseItem.AddedAt,
		licenseItem.ExpiresAt,
		licenseItem.Disclaimer,
		licenseItem.BoundIP,
		licenseItem.ServerIP,
		licenseItem.BoundQQ,
		licenseItem.Param1,
		licenseItem.Param2,
		licenseItem.Param3,
		licenseItem.Param4,
		versionProfile.Code,
		licenseItem.AppendVersionTail,
	)

	blob, err := hfycodec.BuildEncryptedFixedText(fixedText, []byte(a.cfg.BlobKey))
	if err != nil {
		licenseID := licenseItem.ID
		_ = a.store.RecordActivation(r.Context(), &licenseID, remoteIP, false, "server hfy build failed")
		writeJSON(w, http.StatusInternalServerError, activateResponse{
			OK:      false,
			Message: "授权文件生成失败，请联系管理员",
		})
		return
	}

	resp := activateResponse{
		OK:         true,
		Message:    activationOKMessage,
		FixedText:  fixedText,
		BlobBase64: base64.StdEncoding.EncodeToString(blob),
		ServerTime: now.Format(time.RFC3339),
		ExpiresAt:  licenseItem.ExpiresAt.Format(time.RFC3339),
		BoundIP:    licenseItem.BoundIP,
	}

	licenseID := licenseItem.ID
	_ = a.store.RecordActivation(r.Context(), &licenseID, remoteIP, true, "activation ok")
	_ = a.store.MarkSuccessfulActivation(r.Context(), licenseItem.ID, remoteIP, now)
	writeJSON(w, http.StatusOK, resp)
}

func (a *App) handleAdminPage(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	search := strings.TrimSpace(r.URL.Query().Get("q"))
	items, err := a.store.List(r.Context(), search)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	now := time.Now()
	expiresAt, err := licenses.ApplyDuration(now, a.cfg.DefaultDuration, a.cfg.DefaultUnit)
	if err != nil {
		expiresAt = now.AddDate(0, 1, 0)
	}
	defaultVersionCode := hfycodec.DefaultVersionCode
	defaultVersionProfile, _ := hfycodec.ResolveVersionProfile(defaultVersionCode)

	bootstrap := adminBootstrap{
		Search:  search,
		Items:   convertLicenses(items),
		Summary: buildSummary(items),
		Defaults: adminDefaults{
			ProductName:       firstNonEmpty(defaultVersionProfile.ProductName, a.cfg.ProductName),
			VersionCode:       defaultVersionCode,
			AppendVersionTail: true,
			ServerIP:          "",
			Disclaimer:        a.cfg.Disclaimer,
			DurationValue:     a.cfg.DefaultDuration,
			DurationUnit:      a.cfg.DefaultUnit,
			Param1:            a.cfg.DefaultParam1,
			Param2:            a.cfg.DefaultParam2,
			Param3:            a.cfg.DefaultParam3,
			Param4:            a.cfg.DefaultParam4,
			AddedAt:           formatInputDateTime(now),
			ExpiresAt:         formatInputDateTime(expiresAt),
		},
		Versions: convertVersionProfiles(hfycodec.AvailableVersionProfiles()),
		Meta: adminMeta{
			DatabaseMode:    a.databaseModeLabel(),
			AdminAccessMode: a.adminAccessModeLabel(),
			ServerTime:      formatDisplayDateTime(now),
		},
	}

	a.renderTemplate(w, "license_list.html", adminPageData{
		Bootstrap: mustJSON(bootstrap),
	}, http.StatusOK)
}

func (a *App) handleAdminLegacyRedirect(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodGet || r.Method == http.MethodHead {
		http.Redirect(w, r, "/admin/licenses", http.StatusFound)
		return
	}

	if r.Method == http.MethodPost && strings.HasSuffix(strings.TrimRight(r.URL.Path, "/"), "/delete") {
		id, ok := parseLegacyLicenseID(r.URL.Path)
		if !ok {
			http.NotFound(w, r)
			return
		}
		if err := a.store.Delete(r.Context(), id); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		http.Redirect(w, r, "/admin/licenses", http.StatusFound)
		return
	}

	http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
}

func (a *App) handleAdminAPILicenses(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		a.handleAdminAPIList(w, r)
	case http.MethodPost:
		a.handleAdminAPICreate(w, r)
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func (a *App) handleAdminAPILicenseByID(w http.ResponseWriter, r *http.Request) {
	id, ok := parseAdminAPIID(r.URL.Path)
	if !ok {
		http.NotFound(w, r)
		return
	}

	switch r.Method {
	case http.MethodGet:
		item, err := a.store.Get(r.Context(), id)
		if err != nil {
			writeJSON(w, http.StatusNotFound, apiMessage{
				OK:      false,
				Message: "未找到该授权用户",
			})
			return
		}
		writeJSON(w, http.StatusOK, apiDataMessage{
			OK:   true,
			Data: convertLicense(item),
		})
	case http.MethodPut:
		payload, err := decodeAdminPayload(r)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, apiMessage{
				OK:      false,
				Message: err.Error(),
			})
			return
		}
		input, err := a.payloadToInput(payload)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, apiMessage{
				OK:      false,
				Message: err.Error(),
			})
			return
		}
		if err := a.store.Update(r.Context(), id, input); err != nil {
			statusCode := http.StatusInternalServerError
			if licenses.IsValidationError(err) {
				statusCode = http.StatusBadRequest
			}
			writeJSON(w, statusCode, apiMessage{
				OK:      false,
				Message: err.Error(),
			})
			return
		}
		writeJSON(w, http.StatusOK, apiMessage{
			OK:      true,
			Message: "授权用户已更新",
		})
	case http.MethodDelete:
		if err := a.store.Delete(r.Context(), id); err != nil {
			writeJSON(w, http.StatusInternalServerError, apiMessage{
				OK:      false,
				Message: err.Error(),
			})
			return
		}
		writeJSON(w, http.StatusOK, apiMessage{
			OK:      true,
			Message: "授权用户已删除",
		})
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func (a *App) handleAdminAPIList(w http.ResponseWriter, r *http.Request) {
	search := strings.TrimSpace(r.URL.Query().Get("q"))
	items, err := a.store.List(r.Context(), search)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, apiMessage{
			OK:      false,
			Message: err.Error(),
		})
		return
	}

	writeJSON(w, http.StatusOK, apiDataMessage{
		OK: true,
		Data: adminListResponse{
			Items:   convertLicenses(items),
			Summary: buildSummary(items),
		},
	})
}

func (a *App) handleAdminAPISettings(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		settings, err := a.store.GetSettings(r.Context())
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, apiMessage{
				OK:      false,
				Message: err.Error(),
			})
			return
		}
		writeJSON(w, http.StatusOK, apiDataMessage{
			OK: true,
			Data: adminSettings{
				AppendVersionTails: settings.AppendVersionTails,
			},
		})
	case http.MethodPut:
		payload, err := decodeAdminSettingsPayload(r)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, apiMessage{
				OK:      false,
				Message: err.Error(),
			})
			return
		}
		if err := a.store.SetAppendVersionTails(r.Context(), payload.AppendVersionTails); err != nil {
			writeJSON(w, http.StatusInternalServerError, apiMessage{
				OK:      false,
				Message: err.Error(),
			})
			return
		}
		writeJSON(w, http.StatusOK, apiDataMessage{
			OK:      true,
			Message: "全局尾串设置已更新",
			Data: adminSettings{
				AppendVersionTails: payload.AppendVersionTails,
			},
		})
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func (a *App) handleAdminAPICreate(w http.ResponseWriter, r *http.Request) {
	payload, err := decodeAdminPayload(r)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, apiMessage{
			OK:      false,
			Message: err.Error(),
		})
		return
	}

	input, err := a.payloadToInput(payload)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, apiMessage{
			OK:      false,
			Message: err.Error(),
		})
		return
	}

	id, err := a.store.Create(r.Context(), input)
	if err != nil {
		statusCode := http.StatusInternalServerError
		if licenses.IsValidationError(err) {
			statusCode = http.StatusBadRequest
		}
		writeJSON(w, statusCode, apiMessage{
			OK:      false,
			Message: err.Error(),
		})
		return
	}

	writeJSON(w, http.StatusCreated, apiDataMessage{
		OK:      true,
		Message: "授权用户已创建",
		Data: map[string]any{
			"id": id,
		},
	})
}

func (a *App) handleAdminAPIBatchDelete(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	defer r.Body.Close()
	rawBody, err := io.ReadAll(r.Body)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, apiMessage{
			OK:      false,
			Message: "读取请求数据失败",
		})
		return
	}
	rawBody = bytes.TrimPrefix(rawBody, []byte{0xEF, 0xBB, 0xBF})

	var payload batchDeletePayload
	decoder := json.NewDecoder(bytes.NewReader(rawBody))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&payload); err != nil {
		writeJSON(w, http.StatusBadRequest, apiMessage{
			OK:      false,
			Message: "请求数据格式错误",
		})
		return
	}

	ids := make([]int64, 0, len(payload.IDs))
	seen := make(map[int64]struct{}, len(payload.IDs))
	for _, id := range payload.IDs {
		if id <= 0 {
			continue
		}
		if _, ok := seen[id]; ok {
			continue
		}
		seen[id] = struct{}{}
		ids = append(ids, id)
	}

	if len(ids) == 0 {
		writeJSON(w, http.StatusBadRequest, apiMessage{
			OK:      false,
			Message: "请先选择要删除的授权用户",
		})
		return
	}

	if err := a.store.DeleteBatch(r.Context(), ids); err != nil {
		writeJSON(w, http.StatusInternalServerError, apiMessage{
			OK:      false,
			Message: err.Error(),
		})
		return
	}

	writeJSON(w, http.StatusOK, apiMessage{
		OK:      true,
		Message: fmt.Sprintf("已批量删除 %d 条授权记录", len(ids)),
	})
}

func (a *App) payloadToInput(payload adminLicensePayload) (licenses.UpsertInput, error) {
	addedAt, err := licenses.ParseFormDateTime(strings.TrimSpace(payload.AddedAt))
	if err != nil {
		return licenses.UpsertInput{}, err
	}

	expiresAt, err := licenses.ParseFormDateTime(strings.TrimSpace(payload.ExpiresAt))
	if err != nil {
		return licenses.UpsertInput{}, err
	}

	input := licenses.UpsertInput{
		Name:              payload.Name,
		ProductName:       firstNonEmpty(payload.ProductName, a.cfg.ProductName),
		VersionCode:       payload.VersionCode,
		AppendVersionTail: payload.AppendVersionTail,
		Disclaimer:        firstNonEmpty(payload.Disclaimer, a.cfg.Disclaimer),
		BoundIP:           payload.BoundIP,
		ServerIP:          payload.ServerIP,
		BoundQQ:           payload.BoundQQ,
		Param1:            payload.Param1,
		Param2:            payload.Param2,
		Param3:            payload.Param3,
		Param4:            payload.Param4,
		AddedAt:           addedAt,
		ExpiresAt:         expiresAt,
		Active:            payload.Active,
		Notes:             payload.Notes,
		DurationValue:     payload.DurationValue,
		DurationUnit:      firstNonEmpty(payload.DurationUnit, a.cfg.DefaultUnit),
	}

	versionProfile, ok := hfycodec.ResolveVersionProfile(input.VersionCode)
	if !ok {
		return licenses.UpsertInput{}, licenses.ValidationError{Message: fmt.Sprintf("不支持的版本编号: %s", strings.TrimSpace(payload.VersionCode))}
	}
	input.VersionCode = versionProfile.Code

	return input, input.Normalize()
}

func (a *App) renderTemplate(w http.ResponseWriter, name string, data any, status int) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
	w.Header().Set("Pragma", "no-cache")
	w.Header().Set("Expires", "0")
	w.WriteHeader(status)

	pageData, ok := data.(adminPageData)
	if !ok {
		log.Printf("render template %s: unexpected data type %T", name, data)
		return
	}

	html := strings.Replace(a.adminPageTemplate, "__AUTH_BOOTSTRAP__", pageData.Bootstrap, 1)
	html = strings.ReplaceAll(html, "__ASSET_VERSION__", a.assetVersion)
	if _, err := w.Write([]byte(html)); err != nil {
		log.Printf("render template %s: %v", name, err)
	}
}

func (a *App) adminEntryGuard(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		ip := a.realIP(r)
		if !a.cfg.AdminAllowPublic && !a.ipAllowed(ip) {
			a.writeAdminForbidden(w, r)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (a *App) adminGuard(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		ip := a.realIP(r)
		if !a.cfg.AdminAllowPublic && !a.ipAllowed(ip) {
			a.writeAdminForbidden(w, r)
			return
		}

		if a.hasValidSession(r) {
			next.ServeHTTP(w, r)
			return
		}

		if a.validBasicAuth(r) {
			a.setSessionCookie(w, r, false)
			next.ServeHTTP(w, r)
			return
		}

		if a.isAdminAPIRequest(r) {
			writeJSON(w, http.StatusUnauthorized, apiMessage{
				OK:      false,
				Message: "需要管理员登录",
			})
			return
		}

		http.Redirect(w, r, a.loginURL(r.URL.RequestURI()), http.StatusFound)
	})
}

func (a *App) isAuthenticated(r *http.Request) bool {
	return a.hasValidSession(r) || a.validBasicAuth(r)
}

func (a *App) hasValidSession(r *http.Request) bool {
	cookie, err := r.Cookie(adminSessionCookieName)
	if err != nil || strings.TrimSpace(cookie.Value) == "" {
		return false
	}

	username, expiresAt, ok := a.parseSessionToken(cookie.Value)
	if !ok {
		return false
	}
	if username != a.cfg.AdminUsername {
		return false
	}
	return time.Now().Before(expiresAt)
}

func (a *App) validBasicAuth(r *http.Request) bool {
	username, password, ok := r.BasicAuth()
	if !ok {
		return false
	}
	return hmac.Equal([]byte(strings.TrimSpace(username)), []byte(a.cfg.AdminUsername)) &&
		hmac.Equal([]byte(password), []byte(a.cfg.AdminPassword))
}

func (a *App) setSessionCookie(w http.ResponseWriter, r *http.Request, remember bool) {
	ttl := adminSessionTTL
	if remember {
		ttl = adminRememberTTL
	}

	expiresAt := time.Now().Add(ttl)
	cookie := &http.Cookie{
		Name:     adminSessionCookieName,
		Value:    a.buildSessionToken(a.cfg.AdminUsername, expiresAt),
		Path:     "/",
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
		Secure:   a.requestIsHTTPS(r),
	}
	if remember {
		cookie.Expires = expiresAt
		cookie.MaxAge = int(ttl.Seconds())
	}

	http.SetCookie(w, cookie)
}

func (a *App) clearSessionCookie(w http.ResponseWriter, r *http.Request) {
	http.SetCookie(w, &http.Cookie{
		Name:     adminSessionCookieName,
		Value:    "",
		Path:     "/",
		HttpOnly: true,
		SameSite: http.SameSiteLaxMode,
		Secure:   a.requestIsHTTPS(r),
		MaxAge:   -1,
		Expires:  time.Unix(0, 0),
	})
}

func (a *App) buildSessionToken(username string, expiresAt time.Time) string {
	payload := fmt.Sprintf("%s|%d", username, expiresAt.Unix())
	mac := hmac.New(sha256.New, []byte(a.cfg.AdminSessionSecret))
	_, _ = mac.Write([]byte(payload))
	signature := mac.Sum(nil)

	return base64.RawURLEncoding.EncodeToString([]byte(payload)) + "." +
		base64.RawURLEncoding.EncodeToString(signature)
}

func (a *App) parseSessionToken(token string) (string, time.Time, bool) {
	parts := strings.Split(token, ".")
	if len(parts) != 2 {
		return "", time.Time{}, false
	}

	payload, err := base64.RawURLEncoding.DecodeString(parts[0])
	if err != nil {
		return "", time.Time{}, false
	}
	signature, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return "", time.Time{}, false
	}

	mac := hmac.New(sha256.New, []byte(a.cfg.AdminSessionSecret))
	_, _ = mac.Write(payload)
	expected := mac.Sum(nil)
	if !hmac.Equal(signature, expected) {
		return "", time.Time{}, false
	}

	payloadParts := strings.Split(string(payload), "|")
	if len(payloadParts) != 2 {
		return "", time.Time{}, false
	}

	expiresUnix, err := strconv.ParseInt(payloadParts[1], 10, 64)
	if err != nil {
		return "", time.Time{}, false
	}

	return payloadParts[0], time.Unix(expiresUnix, 0), true
}

func (a *App) requestIsHTTPS(r *http.Request) bool {
	if r.TLS != nil {
		return true
	}
	return strings.EqualFold(strings.TrimSpace(r.Header.Get("X-Forwarded-Proto")), "https")
}

func (a *App) isAdminAPIRequest(r *http.Request) bool {
	return strings.HasPrefix(r.URL.Path, "/admin/api/")
}

func (a *App) writeAdminForbidden(w http.ResponseWriter, r *http.Request) {
	if a.isAdminAPIRequest(r) {
		writeJSON(w, http.StatusForbidden, apiMessage{
			OK:      false,
			Message: "admin 仅允许内网访问，可在配置中放开",
		})
		return
	}
	http.Error(w, "admin 仅允许内网访问，可在配置中放开", http.StatusForbidden)
}

func (a *App) loginURL(nextPath string) string {
	safeNext := a.safeNextPath(nextPath)
	if safeNext == "/admin/licenses" {
		return "/login"
	}

	values := url.Values{}
	values.Set("next", safeNext)
	return "/login?" + values.Encode()
}

func (a *App) safeNextPath(raw string) string {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return "/admin/licenses"
	}

	parsed, err := url.Parse(raw)
	if err != nil || parsed.IsAbs() || parsed.Host != "" {
		return "/admin/licenses"
	}
	if parsed.Path == "" || !strings.HasPrefix(parsed.Path, "/") || strings.HasPrefix(parsed.Path, "//") {
		return "/admin/licenses"
	}

	return parsed.RequestURI()
}

func (a *App) ipAllowed(rawIP string) bool {
	ip := net.ParseIP(strings.TrimSpace(rawIP))
	if ip == nil {
		return false
	}
	for _, network := range a.cfg.AdminAllowedNets {
		if network.Contains(ip) {
			return true
		}
	}
	return false
}

func (a *App) realIP(r *http.Request) string {
	if a.cfg.ProxyHeader != "" {
		if raw := strings.TrimSpace(r.Header.Get(a.cfg.ProxyHeader)); raw != "" {
			if strings.Contains(raw, ",") {
				raw = strings.TrimSpace(strings.Split(raw, ",")[0])
			}
			return raw
		}
	}

	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err == nil {
		return host
	}
	return r.RemoteAddr
}

func (a *App) licenseClientIP(r *http.Request) string {
	return normalizeLicenseIP(a.realIP(r))
}

func normalizeLicenseIP(rawIP string) string {
	ipText := strings.TrimSpace(rawIP)
	if strings.EqualFold(ipText, "localhost") {
		return localLicenseIP
	}

	ip := net.ParseIP(ipText)
	if ip != nil && ip.IsLoopback() {
		return localLicenseIP
	}

	return ipText
}

func (a *App) databaseModeLabel() string {
	driverName, _, err := a.cfg.Database()
	if err != nil {
		return "未知"
	}
	if driverName == "mysql" {
		return "MySQL"
	}
	return "SQLite"
}

func (a *App) adminAccessModeLabel() string {
	if a.cfg.AdminAllowPublic {
		return "公网可访问"
	}
	return "仅内网访问"
}

func decodeAdminPayload(r *http.Request) (adminLicensePayload, error) {
	defer r.Body.Close()

	rawBody, err := io.ReadAll(r.Body)
	if err != nil {
		return adminLicensePayload{}, fmt.Errorf("读取请求数据失败")
	}
	rawBody = bytes.TrimPrefix(rawBody, []byte{0xEF, 0xBB, 0xBF})

	var payload adminLicensePayload
	decoder := json.NewDecoder(bytes.NewReader(rawBody))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&payload); err != nil {
		return adminLicensePayload{}, fmt.Errorf("请求数据格式错误: %v", err)
	}
	return payload, nil
}

func decodeAdminSettingsPayload(r *http.Request) (adminSettingsPayload, error) {
	defer r.Body.Close()

	rawBody, err := io.ReadAll(r.Body)
	if err != nil {
		return adminSettingsPayload{}, fmt.Errorf("读取请求数据失败")
	}
	rawBody = bytes.TrimPrefix(rawBody, []byte{0xEF, 0xBB, 0xBF})

	var payload adminSettingsPayload
	decoder := json.NewDecoder(bytes.NewReader(rawBody))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&payload); err != nil {
		return adminSettingsPayload{}, fmt.Errorf("请求数据格式错误: %v", err)
	}
	return payload, nil
}

func parseAdminAPIID(rawPath string) (int64, bool) {
	trimmed := strings.Trim(strings.TrimPrefix(rawPath, "/admin/api/licenses/"), "/")
	if trimmed == "" || strings.Contains(trimmed, "/") {
		return 0, false
	}
	id, err := strconv.ParseInt(trimmed, 10, 64)
	return id, err == nil
}

func parseLegacyLicenseID(rawPath string) (int64, bool) {
	trimmed := strings.Trim(strings.TrimPrefix(rawPath, "/admin/licenses/"), "/")
	parts := strings.Split(trimmed, "/")
	if len(parts) < 2 {
		return 0, false
	}
	id, err := strconv.ParseInt(parts[0], 10, 64)
	return id, err == nil
}

func convertLicenses(items []licenses.License) []adminLicenseItem {
	out := make([]adminLicenseItem, 0, len(items))
	for _, item := range items {
		out = append(out, convertLicense(item))
	}
	return out
}

func convertLicense(item licenses.License) adminLicenseItem {
	out := adminLicenseItem{
		ID:                item.ID,
		Name:              item.Name,
		Product:           item.ProductName,
		VersionCode:       hfycodec.NormalizeVersionCode(item.VersionCode),
		AppendVersionTail: item.AppendVersionTail,
		Disclaimer:        item.Disclaimer,
		BoundIP:           item.BoundIP,
		ServerIP:          firstNonEmpty(item.ServerIP, item.BoundIP),
		BoundQQ:           item.BoundQQ,
		Param1:            item.Param1,
		Param2:            item.Param2,
		Param3:            item.Param3,
		Param4:            item.Param4,
		AddedAt:           formatInputDateTime(item.AddedAt),
		ExpiresAt:         formatInputDateTime(item.ExpiresAt),
		Active:            item.Active,
		Notes:             item.Notes,
		LastSeenIP:        item.LastSeenIP,
		CreatedAt:         formatDisplayDateTime(item.CreatedAt),
		UpdatedAt:         formatDisplayDateTime(item.UpdatedAt),
	}
	if item.LastSeenAt != nil {
		out.LastSeenAt = formatDisplayDateTime(*item.LastSeenAt)
	}
	return out
}

func convertVersionProfiles(items []hfycodec.VersionProfile) []adminVersionItem {
	out := make([]adminVersionItem, 0, len(items))
	for _, item := range items {
		out = append(out, adminVersionItem{
			Code:        item.Code,
			Label:       item.Label,
			ProductName: item.ProductName,
			HasTail:     item.Tail != "",
			TailPreview: item.Tail,
		})
	}
	return out
}

func buildSummary(items []licenses.License) adminSummary {
	now := time.Now()
	summary := adminSummary{
		Total: len(items),
	}
	for _, item := range items {
		if !item.Active {
			summary.Disabled++
			continue
		}
		if now.After(item.ExpiresAt) {
			summary.Expired++
			continue
		}
		summary.Active++
	}
	return summary
}

func formatInputDateTime(t time.Time) string {
	if t.IsZero() {
		return ""
	}
	return t.Format("2006-01-02T15:04:05")
}

func formatDisplayDateTime(t time.Time) string {
	if t.IsZero() {
		return "-"
	}
	return t.Format("2006-01-02 15:04:05")
}

func mustJSON(v any) string {
	data, err := json.Marshal(v)
	if err != nil {
		return `{}`
	}
	return string(data)
}

func writeJSON(w http.ResponseWriter, status int, payload any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	enc := json.NewEncoder(w)
	enc.SetEscapeHTML(false)
	_ = enc.Encode(payload)
}

func firstNonEmpty(values ...string) string {
	for _, value := range values {
		if strings.TrimSpace(value) != "" {
			return strings.TrimSpace(value)
		}
	}
	return ""
}

func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		log.Printf("%s %s %s", r.Method, r.URL.Path, time.Since(start))
	})
}
