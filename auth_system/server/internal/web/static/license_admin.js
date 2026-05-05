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
      product_name: '095',
      version_code: '095',
      append_version_tail: true,
      server_ip: '',
      disclaimer: '本程序仅做学习交流之用，不得用于商业用途！如作他用所承受的法律责任一概与作者无关（下载使用即代表你同意上述观点）',
      param1: true,
      param2: true,
      param3: true,
      param4: true,
      duration_value: 1,
      duration_unit: 'month'
    };
    const fallbackVersions = [
      { code: '079', label: '079', product_name: '079', has_tail: true, tail_prefix: '', second_ip_suffix: '' },
      { code: '083', label: '083', product_name: '083', has_tail: true, tail_prefix: '', second_ip_suffix: ' ' },
      { code: '085', label: '085', product_name: '085', has_tail: true, tail_prefix: '', second_ip_suffix: ' ' },
      { code: '095', label: '095', product_name: '095', has_tail: true, tail_prefix: '', second_ip_suffix: ' ' },
      { code: '099', label: '099', product_name: '099', has_tail: true, tail_prefix: '', second_ip_suffix: '' }
    ];
    const versionTailPlaintext = [
      '00AD8580,00BE4F24,00BF3CD8,00D8A1EC,00E32F74,00F6D130',
      '00AD8530,00BE4ED4,00BF3C6C,00D8A188,00E32F10,00F6D0BC',
      '004F1DB5,009AFEEF,009A3D81,00AFA655,00AB167A,00B67E39',
      '0081324A,0050D7A6,009516C2,00545476,00B0DEA9,00BC3A78',
      '0070EC25,00A00FA0,009536E0,00A911A3,00A5EEDD,00BC3A7D',
      '00A10FB8,00A00FA5,008AD01A,009D2B67,00A2B828,00B1086E',
      '005BD868,008BE044,0077E055,009E86FB,009416A0,00AD85D8',
      '00719DA7,008C8BAF,00AFE8A0,00C8C5F8,00D021B8,009D9CE0',
      '008E0C06,00B064B8,0079023C,0084D268,00603071,009F6C8D',
      '008F36FA,0079645B,007806D0,0066E565,005C597F,0062ACD1'
    ].join('\r\n');

    function asciiBytes(value) {
      const text = String(value || '');
      const out = new Uint8Array(text.length);
      for (let idx = 0; idx < text.length; idx += 1) {
        out[idx] = text.charCodeAt(idx) & 0xFF;
      }
      return out;
    }

    function versionTailCrypt(data, key) {
      if (!data.length || !key.length) return new Uint8Array();

      const s = new Uint8Array(256);
      for (let idx = 0; idx < 256; idx += 1) {
        s[idx] = idx;
      }

      let j = 0;
      for (let i = 1; i <= 256; i += 1) {
        j = ((j + s[i - 1] + key[(i - 1) % key.length]) % 256) + 1;
        const left = i - 1;
        const right = j - 1;
        const current = s[left];
        s[left] = s[right];
        s[right] = current;
      }

      const out = new Uint8Array(data.length);
      let i = 0;
      j = 0;
      for (let idx = 0; idx < data.length; idx += 1) {
        i = ((i + 1) % 256) + 1;
        j = ((j + s[i - 1]) % 256) + 1;
        const left = i - 1;
        const right = j - 1;
        const current = s[left];
        s[left] = s[right];
        s[right] = current;
        const t = ((s[right] + s[left]) % 256) + 1;
        out[idx] = data[idx] ^ s[t - 1];
      }
      return out;
    }

    function randomTailPrefix() {
      const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
      return `${alphabet[Math.floor(Math.random() * alphabet.length)]}${alphabet[Math.floor(Math.random() * alphabet.length)]}`;
    }

    function buildVersionTail(boundIP, prefix) {
      const normalizedIP = String(boundIP || '').trim();
      let normalizedPrefix = String(prefix || '').trim().toUpperCase().slice(0, 2);
      if (!normalizedIP) return '';
      if (normalizedPrefix.length < 2) normalizedPrefix = randomTailPrefix();

      const encoded = versionTailCrypt(
        asciiBytes(versionTailPlaintext),
        asciiBytes(`${normalizedIP}${normalizedPrefix}`)
      );

      let tail = normalizedPrefix;
      for (const value of encoded) {
        tail += String.fromCharCode(65 + (value >> 4), 65 + (value & 0x0F));
      }
      return tail;
    }

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
      const versionCode = normalizedDefaults.version_code || '095';
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
      const versionCode = row.version_code || normalizedDefaults.version_code || '095';
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
        selectedVersion() {
          return this.versionLookup[this.form.version_code] || {};
        },
        selectedVersionHasTail() {
          return !!this.selectedVersion.has_tail;
        },
        fixedTextTail() {
          if (!this.form.append_version_tail) return '';
          if (!this.selectedVersionHasTail) return '';
          return buildVersionTail(
            this.form.server_ip || this.form.bound_ip,
            this.selectedVersion.tail_prefix
          );
        },
        fixedTextPreview() {
          const firstHfyIP = `${this.form.bound_ip || ''}`.trim();
          const secondHfyIP = `${this.form.server_ip || this.form.bound_ip || ''}`.trim();
          const repeatedServerIP = `${secondHfyIP}${this.selectedVersion.second_ip_suffix || ''}`;
          const parts = [
            this.form.product_name || '',
            formatFixedTextDate(this.form.added_at),
            formatFixedTextDate(this.form.expires_at),
            this.form.disclaimer || '',
            firstHfyIP,
            boolDigit(this.form.param1),
            boolDigit(this.form.param2),
            boolDigit(this.form.param3),
            repeatedServerIP,
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
        versionLabel(code) {
          return (code || '').trim() || '095';
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
