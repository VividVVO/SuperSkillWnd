  (() => {
    let initial = {};
    try {
      const bootstrapNode = document.getElementById('auth-bootstrap');
      const raw = bootstrapNode ? bootstrapNode.textContent.trim() : '';
      initial = raw ? JSON.parse(raw) : {};
    } catch (_) {
      initial = {};
    }
    const defaultSummary = { total: 0, active: 0, expired: 0, disabled: 0 };
    const fallbackDefaults = {
      product_name: '099冒险岛-自定义属性',
      version_code: '099',
      append_version_tail: true,
      server_ip: '',
      disclaimer: '本程序仅做学习交流之用，不得用于商业用途！如作他用所承受的法律责任一概与作者无关（下载使用即代表你同意上述观点）',
      param1: false,
      param2: true,
      param3: true,
      param4: true,
      duration_value: 1,
      duration_unit: 'month'
    };
    const fallbackVersions = [
      { code: '079', label: '冒险岛', product_name: '冒险岛', has_tail: false, tail_preview: '' },
      { code: '083', label: '083山茶冒险岛-装备扩展', product_name: '083山茶冒险岛-装备扩展', has_tail: false, tail_preview: '' },
      { code: '085', label: '神木村-角色潜能-自定义属性', product_name: '神木村-角色潜能-自定义属性', has_tail: false, tail_preview: '' },
      { code: '095', label: '095自定义属性-伤害统计-发面板包', product_name: '095自定义属性-伤害统计-发面板包', has_tail: false, tail_preview: '' },
      { code: '099', label: '099冒险岛-自定义属性', product_name: '099冒险岛-自定义属性', has_tail: false, tail_preview: '' }
    ];

    // 日期处理工具
    function pad(value) { return String(value).padStart(2, '0'); }

    function formatInputDate(date) {
      if (!(date instanceof Date) || Number.isNaN(date.getTime())) return '';
      return [
        date.getFullYear(), '-', pad(date.getMonth() + 1), '-', pad(date.getDate()),
        'T', pad(date.getHours()), ':', pad(date.getMinutes()), ':', pad(date.getSeconds())
      ].join('');
    }

    function parseInputDate(value) {
      if (!value) return null;
      const normalized = value.replace(' ', 'T');
      const date = new Date(normalized);
      if (Number.isNaN(date.getTime())) return null;
      return date;
    }

    function addDuration(baseValue, amount, unit) {
      const baseDate = parseInputDate(baseValue);
      if (!baseDate || !amount) return '';
      const next = new Date(baseDate.getTime());
      if (unit === 'day') next.setDate(next.getDate() + Number(amount));
      else if (unit === 'week') next.setDate(next.getDate() + Number(amount) * 7);
      else if (unit === 'month') next.setMonth(next.getMonth() + Number(amount));
      else if (unit === 'year') next.setFullYear(next.getFullYear() + Number(amount));
      return formatInputDate(next);
    }

    function formatDisplay(value) {
      return value ? value.replace('T', ' ') : '-';
    }

    function formatFixedTextDate(value) {
      const date = parseInputDate(value);
      if (!date) return '';
      return [
        date.getFullYear(), '/', date.getMonth() + 1, '/', date.getDate(),
        ' ', pad(date.getHours()), ':', pad(date.getMinutes()), ':', pad(date.getSeconds())
      ].join('');
    }

    function durationLabel(amount, unit) {
      if (!amount) return '-';
      const unitMap = { day: '天', week: '周', month: '月', year: '年' };
      return `${amount} ${unitMap[unit] || unit}`;
    }

    function boolDigit(value) {
      return value ? '1' : '0';
    }

    function normalizeDefaults(defaults) {
      return {
        ...fallbackDefaults,
        ...(defaults || {})
      };
    }

    function normalizeVersions(versions) {
      return Array.isArray(versions) && versions.length ? versions : fallbackVersions;
    }

    function versionProductName(versions, code, fallback = '') {
      const current = (versions || []).find(item => item && item.code === code);
      if (current && (current.product_name || current.label)) {
        return current.product_name || current.label;
      }
      return fallback || '';
    }

    // 表单工厂
    function createEmptyForm(defaults, versions) {
      const normalizedDefaults = normalizeDefaults(defaults);
      const versionCode = normalizedDefaults.version_code || '099';
      return {
        id: null,
        name: '',
        product_name: versionProductName(versions, versionCode, normalizedDefaults.product_name),
        version_code: versionCode,
        append_version_tail: normalizedDefaults.append_version_tail !== false,
        server_ip: normalizedDefaults.server_ip || '',
        disclaimer: normalizedDefaults.disclaimer,
        bound_ip: '',
        bound_qq: '',
        param1: !!normalizedDefaults.param1,
        param2: normalizedDefaults.param2 !== false,
        param3: normalizedDefaults.param3 !== false,
        param4: normalizedDefaults.param4 !== false,
        added_at: normalizedDefaults.added_at || formatInputDate(new Date()),
        expires_at: normalizedDefaults.expires_at || '',
        active: true,
        notes: '',
        duration_value: normalizedDefaults.duration_value || 1,
        duration_unit: normalizedDefaults.duration_unit || 'month'
      };
    }

    function cloneRowToForm(row, defaults, versions) {
      const normalizedDefaults = normalizeDefaults(defaults);
      const versionCode = row.version_code || normalizedDefaults.version_code || '099';
      return {
        id: row.id,
        name: row.name || '',
        product_name: row.product_name || versionProductName(versions, versionCode, normalizedDefaults.product_name),
        version_code: versionCode,
        append_version_tail: typeof row.append_version_tail === 'boolean' ? row.append_version_tail : normalizedDefaults.append_version_tail !== false,
        server_ip: row.server_ip || row.bound_ip || normalizedDefaults.server_ip || '',
        disclaimer: row.disclaimer || normalizedDefaults.disclaimer,
        bound_ip: row.bound_ip || '',
        bound_qq: row.bound_qq || '',
        param1: typeof row.param1 === 'boolean' ? row.param1 : !!normalizedDefaults.param1,
        param2: typeof row.param2 === 'boolean' ? row.param2 : normalizedDefaults.param2 !== false,
        param3: typeof row.param3 === 'boolean' ? row.param3 : normalizedDefaults.param3 !== false,
        param4: typeof row.param4 === 'boolean' ? row.param4 : normalizedDefaults.param4 !== false,
        added_at: row.added_at || normalizedDefaults.added_at || '',
        expires_at: row.expires_at || '',
        active: !!row.active,
        notes: row.notes || '',
        duration_value: normalizedDefaults.duration_value || 1,
        duration_unit: normalizedDefaults.duration_unit || 'month'
      };
    }

    const app = Vue.createApp({
      setup() {
        // 在 setup 中引入图标以便在 template 的 :prefix-icon 中使用
        const { Search, RefreshLeft, User, Box, Position, ChatDotRound, Timer } = ElementPlusIconsVue;
        return {
          SearchIcon: Search,
          RefreshLeftIcon: RefreshLeft,
          UserIcon: User,
          BoxIcon: Box,
          PositionIcon: Position,
          ChatDotRoundIcon: ChatDotRound,
          TimerIcon: Timer
        };
      },
      data() {
        const versions = normalizeVersions(initial.versions);
        return {
          loading: false,
          submitting: false,
          search: initial.search || '',
          rows: initial.items || [],
          summary: initial.summary || { ...defaultSummary },
          defaults: normalizeDefaults(initial.defaults),
          versions,
          meta: initial.meta || {},
          dialogVisible: false,
          dialogMode: 'create',
          form: createEmptyForm(initial.defaults, versions),
          rules: {
            name: [{ required: true, message: '请输入用户名', trigger: 'blur' }],
            product_name: [{ required: true, message: '请输入产品名', trigger: 'blur' }],
            version_code: [{ required: true, message: '请选择版本', trigger: 'change' }],
            bound_ip: [{ required: true, message: '请输入绑定 IP', trigger: 'blur' }],
            server_ip: [{ required: false, message: '请输入服务器 IP', trigger: 'blur' }],
            bound_qq: [{ required: true, message: '请输入绑定 QQ', trigger: 'blur' }],
            added_at: [{ required: true, message: '请选择添加时间', trigger: 'change' }],
            expires_at: [{ required: true, message: '请选择到期时间', trigger: 'change' }],
            disclaimer: [{ required: true, message: '请输入提示语', trigger: 'blur' }]
          }
        };
      },
      computed: {
        versionLookup() {
          return this.versions.reduce((map, item) => {
            if (item && item.code) map[item.code] = item;
            return map;
          }, {});
        },
        fixedTextTail() {
          if (!this.form.append_version_tail) return '';
          const current = this.versionLookup[this.form.version_code];
          return current && current.tail_preview ? current.tail_preview : '';
        },
        fixedTextPreview() {
          const parts = [
            this.form.product_name || '',
            formatFixedTextDate(this.form.added_at),
            formatFixedTextDate(this.form.expires_at),
            this.form.disclaimer || '',
            this.form.bound_ip || '',
            boolDigit(this.form.param1),
            boolDigit(this.form.param2),
            boolDigit(this.form.param3),
            this.form.server_ip || this.form.bound_ip || '',
            boolDigit(this.form.param4),
            this.form.bound_qq || ''
          ];
          if (this.fixedTextTail) parts.push(this.fixedTextTail);
          return parts.join('|');
        }
      },
      watch: {
        'form.version_code'(newCode) {
          if (!this.form) return;
          this.form.product_name = this.versionProductName(newCode, this.form.product_name);
        }
      },
      mounted() {
        this.fetchRows();
      },
      methods: {
        formatDisplay,
        durationLabel,
        boolDigit,
        paramSummary(source) {
          return [
            boolDigit(source && source.param1),
            boolDigit(source && source.param2),
            boolDigit(source && source.param3),
            boolDigit(source && source.param4)
          ].join(' / ');
        },
        versionProductName(code, fallback = '') {
          const current = this.versionLookup[code];
          if (current && (current.product_name || current.label)) {
            return current.product_name || current.label;
          }
          return fallback || '';
        },
        formatVersionLabel(code, label = '') {
          const versionCode = (code || '').trim();
          const rawLabel = (label || '').trim();
          if (!versionCode) return rawLabel || '099';
          if (!rawLabel) return versionCode;
          const compactLabel = rawLabel.replace(/\s+/g, '');
          if (compactLabel.startsWith(versionCode)) return rawLabel;
          return `${versionCode}${rawLabel}`;
        },
        versionLabel(code) {
          const current = this.versionLookup[code];
          return this.formatVersionLabel(code, current ? current.label : '');
        },
        statusType(row) {
          if (!row.active) return 'info';
          const expiry = parseInputDate(row.expires_at);
          if (expiry && expiry.getTime() < Date.now()) return 'warning';
          return 'success';
        },
        statusText(row) {
          if (!row.active) return '已停用';
          const expiry = parseInputDate(row.expires_at);
          if (expiry && expiry.getTime() < Date.now()) return '已过期';
          return '生效中';
        },
        lastSeenText(row) {
          if (!row.last_seen_at && !row.last_seen_ip) return '从未激活记录';
          if (row.last_seen_at && row.last_seen_ip) return `${row.last_seen_at} 来源于 ${row.last_seen_ip}`;
          return row.last_seen_at || row.last_seen_ip || '暂无数据';
        },
        syncQuery() {
          const url = new URL(window.location.href);
          if (this.search) url.searchParams.set('q', this.search);
          else url.searchParams.delete('q');
          window.history.replaceState({}, '', url.toString());
        },
        async refreshList() {
          await this.fetchRows();
        },
        async searchTable() {
          this.syncQuery();
          await this.fetchRows();
        },
        async resetSearch() {
          this.search = '';
          this.syncQuery();
          await this.fetchRows();
        },
        async fetchRows() {
          this.loading = true;
          try {
            const url = new URL('/admin/api/licenses', window.location.origin);
            if (this.search) url.searchParams.set('q', this.search);

            const response = await fetch(url.toString(), {
              method: 'GET',
              credentials: 'same-origin'
            });
            const payload = await response.json();
            if (!response.ok || !payload.ok) throw new Error(payload.message || '获取授权列表失败');

            this.rows = payload.data.items || [];
            this.summary = payload.data.summary || { ...defaultSummary };
          } catch (error) {
            ElementPlus.ElMessage.error(error.message || '获取授权列表失败');
          } finally {
            this.loading = false;
          }
        },
        openCreateDialog() {
          this.dialogMode = 'create';
          this.form = createEmptyForm(this.defaults, this.versions);
          this.dialogVisible = true;
        },
        openEditDialog(row) {
          this.dialogMode = 'edit';
          this.form = cloneRowToForm(row, this.defaults, this.versions);
          this.dialogVisible = true;
        },
        handleDialogClosed() {
          if (this.$refs.formRef) this.$refs.formRef.clearValidate();
        },
        applyDuration() {
          if (!this.form.added_at) {
            ElementPlus.ElMessage.warning('请先选择基准添加时间');
            return;
          }
          this.form.expires_at = addDuration(
                  this.form.added_at,
                  this.form.duration_value,
                  this.form.duration_unit
          );
          ElementPlus.ElMessage.success('已自动计算并填入到期时间');
        },
        async submitForm() {
          if (!this.$refs.formRef) return;
          const valid = await this.$refs.formRef.validate().catch(() => false);
          if (!valid) return;

          const added = parseInputDate(this.form.added_at);
          const expires = parseInputDate(this.form.expires_at);
          if (!added || !expires || expires.getTime() <= added.getTime()) {
            ElementPlus.ElMessage.warning('到期时间必须晚于添加时间');
            return;
          }

          this.submitting = true;
          try {
            const isCreate = this.dialogMode === 'create';
            const url = isCreate ? '/admin/api/licenses' : `/admin/api/licenses/${this.form.id}`;
            const response = await fetch(url, {
              method: isCreate ? 'POST' : 'PUT',
              credentials: 'same-origin',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify(this.form)
            });
            const payload = await response.json();
            if (!response.ok || !payload.ok) throw new Error(payload.message || '保存失败');

            ElementPlus.ElMessage.success(payload.message || '保存成功');
            this.dialogVisible = false;
            await this.fetchRows();
          } catch (error) {
            ElementPlus.ElMessage.error(error.message || '保存失败');
          } finally {
            this.submitting = false;
          }
        },
        async removeRow(row) {
          try {
            await ElementPlus.ElMessageBox.confirm(
                    `确定要永久删除用户「${row.name}」吗？该操作不可逆。`,
                    '危险操作确认',
                    {
                      type: 'error',
                      confirmButtonText: '确认删除',
                      cancelButtonText: '取消',
                      confirmButtonClass: 'el-button--danger'
                    }
            );
            const response = await fetch(`/admin/api/licenses/${row.id}`, {
              method: 'DELETE',
              credentials: 'same-origin'
            });
            const payload = await response.json();
            if (!response.ok || !payload.ok) throw new Error(payload.message || '删除失败');

            ElementPlus.ElMessage.success(payload.message || '删除成功');
            await this.fetchRows();
          } catch (error) {
            if (error === 'cancel' || error === 'close') return;
            ElementPlus.ElMessage.error(error.message || '删除失败');
          }
        },
        async copyPreview() {
          try {
            await navigator.clipboard.writeText(this.fixedTextPreview);
            ElementPlus.ElMessage.success('配置文本已复制到剪贴板');
          } catch (_) {
            ElementPlus.ElMessage.warning('自动复制失败，请手动框选文本');
          }
        }
      }
    });

    // 注册 Element Plus 和 图标
    app.use(ElementPlus);
    for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
      app.component(key, component);
    }

    app.mount('#app');
  })();
