/** Schema-driven device settings form (shared contract with firmware). */
(function (global) {
  const CANONICAL_DEVICE_SETTINGS_SCHEMA = {
    "version": 1,
    "title": "Device settings",
    "sections": [
      {
        "id": "sleep",
        "placement": "submenu",
        "title": "Sleep & power",
        "hint": "WiFi stays on while the screen is off.",
        "fields": [
          {
            "key": "display_timeout_ms",
            "type": "choice",
            "label": "Screen off after",
            "options": [
              {
                "label": "10 sec",
                "value": 10000
              },
              {
                "label": "15 sec",
                "value": 15000
              },
              {
                "label": "20 sec",
                "value": 20000
              },
              {
                "label": "1 min",
                "value": 60000
              },
              {
                "label": "10 min",
                "value": 600000
              },
              {
                "label": "30 min",
                "value": 1800000
              },
              {
                "label": "1 hour",
                "value": 3600000
              }
            ],
            "default": 60000
          },
          {
            "key": "dim_lead_ms",
            "type": "choice",
            "label": "Dim before off",
            "options": [
              {
                "label": "Off",
                "value": 0
              },
              {
                "label": "1 sec",
                "value": 1000
              },
              {
                "label": "2 sec",
                "value": 2000
              },
              {
                "label": "5 sec",
                "value": 5000
              }
            ],
            "default": 2000
          },
          {
            "key": "deep_sleep_timeout_ms",
            "type": "choice",
            "label": "Deep sleep after",
            "options": [
              {
                "label": "2 min",
                "value": 120000
              },
              {
                "label": "5 min",
                "value": 300000
              },
              {
                "label": "15 min",
                "value": 900000
              },
              {
                "label": "30 min",
                "value": 1800000
              },
              {
                "label": "1 hour",
                "value": 3600000
              }
            ],
            "default": 900000
          },
          {
            "key": "motion_wake_enabled",
            "type": "boolean",
            "label": "Wake on motion",
            "default": true
          },
          {
            "key": "key_wake_enabled",
            "type": "boolean",
            "label": "Wake on key press",
            "default": true
          },
          {
            "key": "light_sleep_enabled",
            "type": "boolean",
            "label": "Light sleep in scenes",
            "default": false
          },
          {
            "key": "light_sleep_timeout_ms",
            "type": "choice",
            "label": "Light sleep duration",
            "options": [
              {
                "label": "10 sec",
                "value": 10000
              },
              {
                "label": "1 min",
                "value": 60000
              },
              {
                "label": "10 min",
                "value": 600000
              },
              {
                "label": "30 min",
                "value": 1800000
              },
              {
                "label": "1 hour",
                "value": 3600000
              },
              {
                "label": "2 hours",
                "value": 7200000
              }
            ],
            "default": 60000
          }
        ]
      },
      {
        "id": "display",
        "placement": "submenu",
        "title": "Backlight",
        "hint": "0 = keep current saved level.",
        "fields": [
          {
            "key": "lcd_day_brightness",
            "type": "slider",
            "label": "LCD day",
            "min": 0,
            "max": 255,
            "default": 0
          },
          {
            "key": "lcd_night_brightness",
            "type": "slider",
            "label": "LCD night",
            "min": 0,
            "max": 255,
            "default": 0
          },
          {
            "key": "kbd_day_brightness",
            "type": "slider",
            "label": "Keypad day",
            "min": 0,
            "max": 255,
            "default": 0
          },
          {
            "key": "kbd_night_brightness",
            "type": "slider",
            "label": "Keypad night",
            "min": 0,
            "max": 255,
            "default": 0
          }
        ]
      },
      {
        "id": "mqtt",
        "placement": "menu",
        "menu_icon": "home",
        "menu_title": "MQTT",
        "title": "MQTT",
        "fields": [
          {
            "key": "mqtt_enabled",
            "type": "boolean",
            "label": "Enable MQTT",
            "default": false
          },
          {
            "key": "mqtt_broker",
            "type": "string",
            "label": "Broker",
            "default": "broker"
          },
          {
            "key": "mqtt_port",
            "type": "string",
            "label": "Port",
            "default": "1883"
          },
          {
            "key": "mqtt_user",
            "type": "string",
            "label": "User",
            "default": "user"
          },
          {
            "key": "mqtt_password",
            "type": "string",
            "label": "Password",
            "default": "password"
          },
          {
            "key": "mqtt_client_id",
            "type": "string",
            "label": "Client ID",
            "default": "OMOTE"
          }
        ]
      },
      {
        "id": "ntp",
        "placement": "menu",
        "menu_icon": "refresh",
        "menu_title": "NTP",
        "title": "NTP",
        "fields": [
          {
            "key": "ntp_enabled",
            "type": "boolean",
            "label": "Enable NTP",
            "default": false
          },
          {
            "key": "ntp_display_mode",
            "type": "choice",
            "label": "Display mode",
            "options": [
              {
                "label": "Constant",
                "value": 0
              },
              {
                "label": "Alternating",
                "value": 1
              },
              {
                "label": "First 5 sec",
                "value": 2
              }
            ],
            "default": 0
          },
          {
            "key": "ntp_server",
            "type": "string",
            "label": "Server",
            "default": "pool.ntp.org",
            "options": [
              {
                "label": "pool.ntp.org",
                "value": "pool.ntp.org"
              },
              {
                "label": "Google",
                "value": "time.google.com"
              },
              {
                "label": "Cloudflare",
                "value": "time.cloudflare.com"
              },
              {
                "label": "Windows",
                "value": "time.windows.com"
              },
              {
                "label": "Ubuntu",
                "value": "ntp.ubuntu.com"
              },
              {
                "label": "Apple",
                "value": "time.apple.com"
              }
            ]
          },
          {
            "key": "timezone",
            "type": "string",
            "label": "Timezone",
            "default": "GMT0BST,M3.5.0/1,M10.5.0",
            "options": [
              {
                "label": "UK (GMT/BST)",
                "value": "GMT0BST,M3.5.0/1,M10.5.0"
              },
              {
                "label": "UTC",
                "value": "UTC0"
              },
              {
                "label": "US Eastern",
                "value": "EST5EDT,M3.2.0,M11.1.0"
              },
              {
                "label": "US Central",
                "value": "CST6CDT,M3.2.0,M11.1.0"
              },
              {
                "label": "US Mountain",
                "value": "MST7MDT,M3.2.0,M11.1.0"
              },
              {
                "label": "US Pacific",
                "value": "PST8PDT,M3.2.0,M11.1.0"
              },
              {
                "label": "Central Europe",
                "value": "CET-1CEST,M3.5.0,M10.5.0"
              },
              {
                "label": "Israel",
                "value": "IST-2IDT,M3.4.4/26,M10.5.0"
              },
              {
                "label": "Australia (Sydney)",
                "value": "AEST-10AEDT,M10.1.0,M4.1.0/3"
              }
            ]
          }
        ]
      },
      {
        "id": "bluetooth",
        "placement": "menu",
        "menu_icon": "bluetooth",
        "menu_title": "Bluetooth",
        "title": "Bluetooth HID",
        "hint": "VID/PID profile selects which Android keylayout (.kl) maps HID reports to TV keys. BLE runs only in scenes with BleEnabled. Disabled during editor sync.",
        "fields": [
          {
            "key": "ble_profile",
            "type": "string",
            "label": "HID identity (VID/PID)",
            "default": "generic",
            "options": [
              {
                "label": "Generic (recommended — widest Google TV mapping)",
                "value": "generic"
              },
              {
                "label": "Onn full keyboard + remote (0x0484 / 0x5738)",
                "value": "onn-full-keyboard"
              },
              {
                "label": "Google reference RCU (0x0957 / 0x0001)",
                "value": "google-reference-rcu"
              },
              {
                "label": "Apple keyboard (0x05AC / 0x820A)",
                "value": "apple-keyboard"
              }
            ]
          }
        ]
      },
      {
        "id": "ftp",
        "placement": "menu",
        "menu_icon": "directory",
        "menu_title": "FTP",
        "title": "FTP",
        "fields": [
          {
            "key": "ftp_enabled",
            "type": "boolean",
            "label": "Enable FTP",
            "default": false
          },
          {
            "key": "ftp_mdns_name",
            "type": "string",
            "label": "mDNS name",
            "default": "omote"
          },
          {
            "key": "ftp_user",
            "type": "string",
            "label": "User",
            "default": "OMOTE"
          },
          {
            "key": "ftp_password",
            "type": "string",
            "label": "Password",
            "default": "OMOTE"
          }
        ]
      }
    ]
  };
  const DEFAULT_DEVICE_SETTINGS_SCHEMA = CANONICAL_DEVICE_SETTINGS_SCHEMA;

  function schemaFieldKeys(schema) {
    const keys = new Set();
    for (const section of schema?.sections || []) {
      for (const field of section.fields || []) {
        if (field?.key) keys.add(field.key);
      }
    }
    return keys;
  }

  function defaultsFromSchema(schema) {
    const out = {};
    for (const section of schema?.sections || []) {
      for (const field of section.fields || []) {
        if (field?.key && field.default !== undefined) out[field.key] = field.default;
      }
    }
    return out;
  }

  function readFieldValue(values, field) {
    const key = field.key;
    if (values && values[key] !== undefined && values[key] !== null) return values[key];
    return field.default;
  }

  function optionValues(field) {
    return (field.options || []).map((o) => String(o.value));
  }

  function appendStringOptions(select, field, current) {
    const cur = current == null ? '' : String(current);
    const values = optionValues(field);
    let matched = false;
    for (const opt of field.options || []) {
      const o = document.createElement('option');
      o.value = String(opt.value);
      o.textContent = opt.label ?? opt.value;
      if (o.value === cur) {
        o.selected = true;
        matched = true;
      }
      select.appendChild(o);
    }
    if (cur && !matched) {
      const o = document.createElement('option');
      o.value = cur;
      o.textContent = `Custom: ${cur}`;
      o.selected = true;
      select.appendChild(o);
    }
  }

  function renderField(container, field, values) {
    const v = { ...values };
    const wrap = document.createElement('label');
    wrap.className = field.type === 'boolean' ? 'checkbox-row field-block' : 'field-block';
    wrap.dataset.key = field.key;

    if (field.type === 'boolean') {
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.dataset.key = field.key;
      cb.checked = !!readFieldValue(v, field);
      wrap.appendChild(cb);
      wrap.append(' ' + (field.label || field.key));
    } else if (field.type === 'choice' && field.options?.length) {
      wrap.textContent = field.label || field.key;
      const sel = document.createElement('select');
      sel.dataset.key = field.key;
      const current = readFieldValue(v, field);
      const stringOptions = field.options.some((opt) => Number.isNaN(Number(opt.value)));
      if (stringOptions) {
        appendStringOptions(sel, field, current);
      } else {
        for (const opt of field.options) {
          const o = document.createElement('option');
          o.value = String(opt.value);
          o.textContent = opt.label;
          if (Number(opt.value) === Number(current)) o.selected = true;
          sel.appendChild(o);
        }
      }
      wrap.appendChild(sel);
    } else if (field.type === 'string' && field.options?.length) {
      wrap.textContent = field.label || field.key;
      const sel = document.createElement('select');
      sel.dataset.key = field.key;
      appendStringOptions(sel, field, readFieldValue(v, field));
      wrap.appendChild(sel);
    } else if (field.type === 'slider' || field.type === 'number') {
      wrap.textContent = field.label || field.key;
      const inp = document.createElement('input');
      inp.type = 'number';
      inp.dataset.key = field.key;
      if (field.min !== undefined) inp.min = field.min;
      if (field.max !== undefined) inp.max = field.max;
      inp.value = readFieldValue(v, field);
      wrap.appendChild(inp);
    } else if (field.type === 'string') {
      wrap.textContent = field.label || field.key;
      const inp = document.createElement('input');
      inp.type = 'text';
      inp.dataset.key = field.key;
      inp.value = readFieldValue(v, field) ?? '';
      wrap.appendChild(inp);
    }
    container.appendChild(wrap);
  }

  function renderSectionFields(container, section, values) {
    if (section.hint) {
      const p = document.createElement('p');
      p.className = 'muted small section-hint';
      p.textContent = section.hint;
      container.appendChild(p);
    }
    for (const field of section.fields || []) {
      renderField(container, field, values);
    }
  }

  function applyValuesToForm(container, values) {
    if (!container || !values) return;
    for (const [key, value] of Object.entries(values)) {
      const el = container.querySelector(`[data-key="${key}"]`);
      if (!el) continue;
      if (el.tagName === 'INPUT' && el.type === 'checkbox') {
        el.checked = !!value;
      } else {
        el.value = String(value);
      }
    }
  }

  let modalElements = null;

  function ensureModal() {
    if (modalElements) return modalElements;
    const modal = document.createElement('div');
    modal.id = 'device-settings-section-modal';
    modal.className = 'modal hidden';
    modal.setAttribute('role', 'dialog');
    modal.setAttribute('aria-modal', 'true');
    modal.innerHTML = `
      <div class="modal-panel device-settings-modal-panel">
        <h3 id="device-settings-modal-title" class="section-title"></h3>
        <p id="device-settings-modal-hint" class="muted small section-lead hidden"></p>
        <div id="device-settings-modal-body"></div>
        <div class="modal-actions">
          <button type="button" id="device-settings-modal-done" class="primary">Done</button>
        </div>
      </div>`;
    document.body.appendChild(modal);
    const done = modal.querySelector('#device-settings-modal-done');
    done.addEventListener('click', () => closeSectionModal());
    modal.addEventListener('click', (e) => {
      if (e.target === modal) closeSectionModal();
    });
    modalElements = {
      modal,
      title: modal.querySelector('#device-settings-modal-title'),
      hint: modal.querySelector('#device-settings-modal-hint'),
      body: modal.querySelector('#device-settings-modal-body'),
      rootContainer: null,
      schema: null,
    };
    return modalElements;
  }

  function openSectionModal(section, schema, rootContainer) {
    const ui = ensureModal();
    ui.rootContainer = rootContainer;
    ui.schema = schema;
    const values = collectDeviceSettingsFromForm(rootContainer, schema);
    ui.title.textContent = section.title || section.id || 'Settings';
    if (section.hint) {
      ui.hint.textContent = section.hint;
      ui.hint.classList.remove('hidden');
    } else {
      ui.hint.textContent = '';
      ui.hint.classList.add('hidden');
    }
    ui.body.innerHTML = '';
    renderSectionFields(ui.body, section, values);
    ui.modal.classList.remove('hidden');
  }

  function closeSectionModal() {
    if (!modalElements?.modal) return;
    const { body, rootContainer, schema, modal } = modalElements;
    if (rootContainer && schema) {
      const patch = collectDeviceSettingsFromForm(body, schema);
      applyValuesToForm(rootContainer, patch);
    }
    modal.classList.add('hidden');
  }

  function renderDeviceSettingsForm(container, schema, values) {
    if (!container) return;
    container.innerHTML = '';
    const s = schema || DEFAULT_DEVICE_SETTINGS_SCHEMA;
    const v = { ...defaultsFromSchema(s), ...(values || {}) };

    const toolbar = document.createElement('div');
    toolbar.className = 'row device-settings-toolbar';
    const expandBtn = document.createElement('button');
    expandBtn.type = 'button';
    expandBtn.className = 'small-btn';
    expandBtn.textContent = 'Expand all';
    expandBtn.addEventListener('click', () => {
      container.querySelectorAll('details.settings-section').forEach((d) => { d.open = true; });
    });
    const collapseBtn = document.createElement('button');
    collapseBtn.type = 'button';
    collapseBtn.className = 'small-btn';
    collapseBtn.textContent = 'Collapse all';
    collapseBtn.addEventListener('click', () => {
      container.querySelectorAll('details.settings-section').forEach((d) => { d.open = false; });
    });
    toolbar.appendChild(expandBtn);
    toolbar.appendChild(collapseBtn);
    container.appendChild(toolbar);

    let index = 0;
    for (const section of s.sections || []) {
      const details = document.createElement('details');
      details.className = 'settings-section';
      details.open = index === 0;
      index += 1;

      const summary = document.createElement('summary');
      summary.className = 'settings-section-summary';

      const titleSpan = document.createElement('span');
      titleSpan.className = 'settings-section-title';
      titleSpan.textContent = section.title || section.id || 'Settings';
      summary.appendChild(titleSpan);

      const fieldCount = (section.fields || []).length;
      if (fieldCount) {
        const badge = document.createElement('span');
        badge.className = 'settings-section-badge';
        badge.textContent = `${fieldCount} field${fieldCount === 1 ? '' : 's'}`;
        summary.appendChild(badge);
      }

      const popBtn = document.createElement('button');
      popBtn.type = 'button';
      popBtn.className = 'small-btn settings-section-popout';
      popBtn.textContent = 'Edit';
      popBtn.title = 'Open this section in a dialog';
      popBtn.addEventListener('click', (e) => {
        e.preventDefault();
        e.stopPropagation();
        openSectionModal(section, s, container);
      });
      summary.appendChild(popBtn);

      details.appendChild(summary);

      const body = document.createElement('div');
      body.className = 'settings-section-body';
      renderSectionFields(body, section, v);
      details.appendChild(body);

      container.appendChild(details);
    }
  }

  function mergeProtectedSchemaSections(userSchema, canonicalSchema) {
    const out = JSON.parse(JSON.stringify(userSchema || {
      version: 1,
      title: 'Device settings',
      sections: [],
    }));
    if (!Array.isArray(out.sections)) out.sections = [];

    for (const canonSection of canonicalSchema?.sections || []) {
      if (!canonSection?.id) continue;
      let userSection = out.sections.find((s) => s?.id === canonSection.id);
      if (!userSection) {
        out.sections.push(JSON.parse(JSON.stringify(canonSection)));
        continue;
      }
      if (!Array.isArray(userSection.fields)) userSection.fields = [];
      for (const canonField of canonSection.fields || []) {
        if (!canonField?.key) continue;
        if (!userSection.fields.some((f) => f?.key === canonField.key)) {
          userSection.fields.push(JSON.parse(JSON.stringify(canonField)));
        }
      }
      if (canonSection.placement === 'menu') {
        userSection.placement = canonSection.placement;
        if (canonSection.menu_icon) userSection.menu_icon = canonSection.menu_icon;
        if (canonSection.menu_title) userSection.menu_title = canonSection.menu_title;
        if (canonSection.title) userSection.title = canonSection.title;
        if (canonSection.hint) userSection.hint = canonSection.hint;
      }
    }
    return out;
  }

  function mergeDeviceSettingsValues(backupValues, mergedSchema) {
    const out = { ...defaultsFromSchema(mergedSchema) };
    const validKeys = schemaFieldKeys(mergedSchema);
    for (const [key, value] of Object.entries(backupValues || {})) {
      if (validKeys.has(key)) out[key] = value;
    }
    return out;
  }

  function collectDeviceSettingsFromForm(container, schema) {
    const out = { ...defaultsFromSchema(schema) };
    if (!container) return out;
    container.querySelectorAll('input[data-key], select[data-key]').forEach((el) => {
      const key = el.dataset.key;
      if (!key) return;
      if (el.tagName === 'INPUT' && el.type === 'checkbox') {
        out[key] = el.checked;
      } else if (el.tagName === 'SELECT') {
        const n = Number(el.value);
        out[key] = Number.isFinite(n) && el.value !== '' && String(n) === el.value ? n : el.value;
      } else if (el.tagName === 'INPUT') {
        const n = Number(el.value);
        out[key] = Number.isFinite(n) && el.type === 'number' ? n : el.value;
      }
    });
    return out;
  }

  global.OmoteSettingsForm = {
    CANONICAL_DEVICE_SETTINGS_SCHEMA,
    DEFAULT_DEVICE_SETTINGS_SCHEMA,
    schemaFieldKeys,
    defaultsFromSchema,
    mergeProtectedSchemaSections,
    mergeDeviceSettingsValues,
    renderDeviceSettingsForm,
    collectDeviceSettingsFromForm,
    applyValuesToForm,
  };
})(typeof window !== 'undefined' ? window : globalThis);
