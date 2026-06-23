"use strict";

const Pages = {};

// ============================================================
// Theme Management
// ============================================================
const Theme = {
  _theme: "dark",

  init: function () {
    // `localStorage` is intentional for non-sensitive preferences
    this._theme = localStorage.getItem("theme") || "dark";
    this.apply(this._theme);
    this.initSelector();
  },

  apply: function (theme) {
    this._theme = theme;
    document.documentElement.setAttribute("data-theme", theme);
    localStorage.setItem("theme", theme);
  },

  initSelector: function () {
    const select = document.getElementById("theme-select");
    if (!select) return;

    select.value = this._theme;

    const self = this;
    select.addEventListener("change", function () {
      self.apply(this.value);
    });
  }
};

// ============================================================
// Internationalization
// ============================================================
const I18N = {
  _lang: "en",
  _strings: {},
  _available: [],

  init: function () {
    if (typeof LANG_DATA === "undefined") {
      this._strings = {};
      this._available = ["en"];
      this._lang = "en";
      return;
    }

    this._available = Object.keys(LANG_DATA);
    this._lang = this.detect();
    this._strings = this.resolve(this._lang);
    this.applyDom();
    this.initSelector();
  },

  detect: function () {
    // `localStorage` is intentional for non-sensitive preferences
    const stored = localStorage.getItem("lang");
    if (stored && LANG_DATA[stored]) return stored;

    const nav = (navigator.language || "en").split("-")[0].toLowerCase();
    if (LANG_DATA[nav]) return nav;

    return "en";
  },

  resolve: function (lang) {
    const base = LANG_DATA.en || {};
    const overlay = LANG_DATA[lang] || {};
    const result = {};
    for (let k in base) {
      if (base.hasOwnProperty(k)) {
        result[k] = overlay[k] || base[k];
      }
    }
    return result;
  },

  setLang: function (lang) {
    if (!LANG_DATA[lang]) return;
    this._lang = lang;
    localStorage.setItem("lang", lang);
    this._strings = this.resolve(lang);
    this.applyDom();
    const select = document.getElementById("lang-select");
    if (select) select.value = lang;
    // Re-render dynamic content on current page
    UI.loadPage(UI.currentPage);
    UI.updateLogoutVisibility();
    UI.setStatus(WS.authenticated ? "ok" : (WS.socket && WS.socket.readyState === 0 ? "connecting" : "err"));
  },

  applyDom: function () {
    const strings = this._strings;

    document.querySelectorAll("[data-i18n]").forEach(function (el) {
      const key = el.getAttribute("data-i18n");
      if (strings[key]) el.textContent = strings[key];
    });

    document.querySelectorAll("[data-i18n-html]").forEach(function (el) {
      const key = el.getAttribute("data-i18n-html");
      if (strings[key]) el.innerHTML = strings[key];
    });

    document.querySelectorAll("[data-i18n-ph]").forEach(function (el) {
      const key = el.getAttribute("data-i18n-ph");
      if (strings[key]) el.placeholder = strings[key];
    });

    document.querySelectorAll("[data-i18n-title]").forEach(function (el) {
      const key = el.getAttribute("data-i18n-title");
      if (strings[key]) el.title = strings[key];
    });
  },

  initSelector: function () {
    const select = document.getElementById("lang-select");
    if (!select) return;

    select.innerHTML = "";
    const self = this;
    this._available.sort().forEach(function (code) {
      const opt = document.createElement("option");
      opt.value = code;
      opt.textContent = code.toUpperCase();
      select.appendChild(opt);
    });
    select.value = this._lang;

    select.addEventListener("change", function () {
      self.setLang(this.value);
    });
  },

  t: function (key, replacements) {
    let str = this._strings[key] || key;
    if (replacements) {
      for (const k in replacements) {
        if (replacements.hasOwnProperty(k)) {
          str = str.replace("{" + k + "}", replacements[k]);
        }
      }
    }
    return str;
  }
};

// ============================================================
// WebSocket Communication Layer
// ============================================================
const WS = {
  socket: null,
  url: (function () {
    const urlParams = new URLSearchParams(window.location.search);
    let host = location.host;
    if (location.host === "localhost:9000" || location.host === "127.0.0.1:9000") {
      host = urlParams.get("dock") || location.host;
    }
    const proto = location.protocol === "https:" ? "wss://" : "ws://";
    return proto + host + "/ws";
  })(),
  token: null,
  authenticated: false,
  actionsEnabled: false,
  msgId: 0,
  pending: {},
  reconnectMs: 2000,
  maxReconnectMs: 30000,
  reconnectTimer: null,
  eventHandlers: {},
  statusRefreshTimer: null,
  keepConnected: false,

  // Timeout until inactivity warning
  INACTIVITY_TIMEOUT_MS: 570000,  // 9.5 min
  // Timeout until disconnect after warning
  DISCONNECT_TIMEOUT_MS: 30000,
  inactivityTimer: null,
  warningTimer: null,
  warningShown: false,

  connect: function () {
    if (this.socket && this.socket.readyState < 2) return;
    clearTimeout(this.reconnectTimer);
    this.socket = new WebSocket(this.url);
    this.socket.onopen = function () {
      UI.setStatus("connecting");
      // If not authenticated and not keeping connection, close after status fetch
      if (!WS.keepConnected) {
        Pages.Status.loadWithClose();
      }
    };
    const self = this;
    this.socket.onclose = function () {
      self.authenticated = false;
      self.rejectAllPending();
      self.updateActionState();
      if (self.keepConnected) {
        self.scheduleReconnect();
      }
    };
    this.socket.onerror = function (e) {
      Toast.error(e);
    };
    this.socket.onmessage = function (msg) {
      try {
        self.onMessage(JSON.parse(msg.data));
      } catch (ex) {
        console.error(ex)
      }
    };
  },

  onMessage: function (msg) {
    if (msg.type === "auth_required") {
      if (this.token) {
        this.send({type: "auth", token: this.token});
        UI.setStatus("connecting");
      } else {
        UI.setStatus("err");
        // Don't show login dialog if we're just fetching status
        if (UI.currentPage !== "status" && this.keepConnected) {
          UI.showLogin();
        }
      }
      // Load status page regardless (no auth needed)
      if (UI.currentPage === "status") {
        if (this.keepConnected) {
          Pages.Status.load();
        }
        // If not keeping connected, Status.loadWithClose() was already called on open
      }
      return;
    }

    if (msg.type === "authentication") {
      if (msg.code === 200) {
        this.authenticated = true;
        this.reconnectMs = 2000;
        sessionStorage.setItem("token", this.token);
        UI.setStatus("ok");
        UI.hideLogin();
        UI.onAuthenticated();
        this.startStatusRefresh();
        this.keepConnected = true;
        this.startInactivityTimer();
        this.updateActionState();
      } else {
        this.token = null;
        sessionStorage.removeItem("token");
        UI.setStatus("err");
        UI.showLogin(I18N.t("e_invalid_token"));
        this.stopStatusRefresh();
        this.stopInactivityTimer();
        this.updateActionState();
      }
      return;
    }

    // Check for global reboot required message
    if (msg.code === 200 && msg.reboot === true) {
      if (confirm(I18N.t("confirm_reboot_now"))) {
        location.reload();
      }
    }

    // Response correlation via req_id
    if (msg.req_id !== undefined && this.pending[msg.req_id]) {
      let p = this.pending[msg.req_id];
      clearTimeout(p.timer);
      delete this.pending[msg.req_id];
      if (msg.code >= 200 && msg.code < 300) {
        p.resolve(msg);
      } else {
        p.reject(msg);
      }
      return;
    }

    // Push events
    if (msg.type === "event") {
      let h = this.eventHandlers[msg.msg];
      if (h) h(msg);

      // Reset inactivity timer on specific events (user-visible activity)
      if (msg.msg === "log" || msg.msg === "serial_data" || msg.msg === "ir_receive") {
        this.trackActivity();
      }
    }
  },


  request: function (command, data) {
    let id = ++this.msgId;
    let msg = {type: "dock", id: id, command: command};
    if (data) {
      for (let k in data) {
        if (data.hasOwnProperty(k)) msg[k] = data[k];
      }
    }
    const self = this;
    return new Promise(function (resolve, reject) {
      let timer = setTimeout(function () {
        delete self.pending[id];
        reject({code: 408, msg: "timeout"});
      }, 10000);
      self.pending[id] = {resolve: resolve, reject: reject, timer: timer};
      try {
        self.socket.send(JSON.stringify(msg));
        // Track user-initiated activity (exclude auto-refresh and auto-auth)
        if (command !== "get_sysinfo" && command !== "auth") {
          self.trackActivity();
        }
      } catch (e) {
        clearTimeout(timer);
        delete self.pending[id];
        reject({code: 503, msg: "send failed"});
      }
    });
  },

  send: function (msg) {
    try {
      this.socket.send(JSON.stringify(msg));
    } catch (e) {
      // connection lost, onclose will handle reconnect
      Toast.error(e);
    }
  },

  scheduleReconnect: function () {
    UI.setStatus("err");
    const self = this;
    this.reconnectTimer = setTimeout(function () {
      self.connect();
    }, this.reconnectMs);
    this.reconnectMs = Math.min(Math.round(this.reconnectMs * 1.5), this.maxReconnectMs);
  },

  startStatusRefresh: function () {
    const self = this;
    this.stopStatusRefresh();
    this.statusRefreshTimer = setInterval(function () {
      if (self.authenticated && UI.currentPage === "status") {
        Pages.Status.load(true);  // Force refresh to get new data
      }
    }, 60000);
  },

  stopStatusRefresh: function () {
    if (this.statusRefreshTimer) {
      clearInterval(this.statusRefreshTimer);
      this.statusRefreshTimer = null;
    }
  },

  disconnect: function () {
    this.keepConnected = false;
    clearTimeout(this.reconnectTimer);
    if (this.socket && this.socket.readyState === 1) {
      this.socket.close();
    }
    this.stopInactivityTimer();
  },

  rejectAllPending: function () {
    for (let id in this.pending) {
      if (this.pending.hasOwnProperty(id)) {
        clearTimeout(this.pending[id].timer);
        this.pending[id].reject({code: 503, msg: "disconnected"});
      }
    }
    this.pending = {};
    this.stopStatusRefresh();
    this.stopInactivityTimer();
    this.resetClientState();
    this.updateActionState();
  },

  resetClientState: function () {
    // Reset RS232 console state for both ports
    if (Pages.Ports) {
      Pages.Ports.serialEnabled[1] = false;
      Pages.Ports.serialEnabled[2] = false;
      for (let n = 1; n <= 2; n++) {
        const btn = document.getElementById("btn-port" + n + "-console");
        if (btn) {
          btn.textContent = I18N.t("btn_enable_console");
        }
      }
    }
    // Reset log streaming state
    if (Pages.Logs) {
      Pages.Logs.streaming = false;
      const logBtn = document.getElementById("btn-log-stream");
      if (logBtn) {
        logBtn.textContent = I18N.t("btn_start_streaming");
        logBtn.classList.remove("btn-warn");
      }
    }
    // Reset OTA update state
    if (Pages.OTA) {
      Pages.OTA.resetState();
    }
  },

  on: function (msg, handler) {
    this.eventHandlers[msg] = handler;
  },
  off: function (msg) {
    delete this.eventHandlers[msg];
  },

  authenticate: function (token) {
    this.token = token;
    if (this.socket && this.socket.readyState === 1) {
      this.send({type: "auth", token: token});
      UI.setStatus("connecting");
    } else {
      this.connect();
    }
  },

  startInactivityTimer: function () {
    if (!this.authenticated) return;

    const self = this;
    this.clearInactivityTimers();
    this.warningShown = false;
    console.debug("startInactivityTimer")

    this.inactivityTimer = setTimeout(function () {
      self.handleInactivityTimeout();
    }, this.INACTIVITY_TIMEOUT_MS);
  },

  stopInactivityTimer: function () {
    console.debug("stopInactivityTimer")
    this.clearInactivityTimers();
    this.warningShown = false;
  },

  clearInactivityTimers: function () {
    console.debug("clearInactivityTimer")
    if (this.inactivityTimer) {
      clearTimeout(this.inactivityTimer);
      this.inactivityTimer = null;
    }
    if (this.warningTimer) {
      clearTimeout(this.warningTimer);
      this.warningTimer = null;
    }
  },

  resetInactivityTimer: function () {
    if (!this.authenticated) return;
    console.debug("resetInactivityTimer")

    const self = this;
    clearTimeout(this.warningTimer);
    this.warningShown = false;

    clearTimeout(this.inactivityTimer);
    this.inactivityTimer = setTimeout(function () {
      self.handleInactivityTimeout();
    }, this.INACTIVITY_TIMEOUT_MS);
  },

  handleInactivityTimeout: function () {
    const self = this;
    console.debug("handleInactivityTimeout: %s", this.warningShown);

    // Show warning at 9.5 minutes
    if (!this.warningShown) {
      this.warningShown = true;
      this.showInactivityWarning();

      // Schedule actual disconnect after 30 seconds
      this.warningTimer = setTimeout(function () {
        self.performDisconnect();
      }, this.DISCONNECT_TIMEOUT_MS);
      return;
    }

    // Perform disconnect if warning already shown
    this.performDisconnect();
  },

  showInactivityWarning: function () {
    Toast.show(I18N.t("t_inactivity_warning"), false);
  },

  performDisconnect: function () {
    console.debug("performDisconnect");
    // Reset auth state but keep token
    this.authenticated = false;
    this.keepConnected = false;

    // Close WebSocket
    if (this.socket && this.socket.readyState === 1) {
      this.socket.close();
    }

    // Update UI
    UI.setStatus("err");
    UI.updateLogoutVisibility();
    Toast.show(I18N.t("t_inactivity_disconnected"), true);
  },

  trackActivity: function () {
    if (this.authenticated) {
      this.resetInactivityTimer();
    }
  },

  updateActionState: function () {
    const enabled = this.authenticated && this.socket && this.socket.readyState === 1;
    this.actionsEnabled = enabled;
    UI.setActionsEnabled(enabled);
  }

};

// ============================================================
// Toast Notification
// ============================================================
const Toast = {
  timer: null,

  show: function (message, isError) {
    let el = document.getElementById("toast");
    el.textContent = message;
    el.className = "toast " + (isError ? "toast-err" : "toast-ok");
    el.classList.remove("hidden");
    clearTimeout(this.timer);
    this.timer = setTimeout(function () {
      el.classList.add("hidden");
    }, isError ? 5000 : 3000);
  },

  error: function (msg) {
    console.error(msg);
    let text = "Error";
    if (msg) {
      if (msg.code) text += " " + msg.code;
      if (msg.msg) {
        text += ": " + msg.msg || I18N.t("e_unknown");
        if (msg.error) {
          text += ". " + msg.error;
        }
      } else {
        text += ": " + msg
      }
    }

    this.show(text, true);
  },

  success: function (message) {
    this.show(message || "OK", false);
  }
};

// ============================================================
// UI Controller
// ============================================================
const UI = {
  currentPage: "status",
  pendingPage: null,
  authRequired: {general: true, network: true, ir: true, ports: true, ota: true, logs: true, expert: true},
  sysInfoCache: null,
  actionButtonIds: [
    // General
    'btn-save-name', 'btn-save-brightness', 'btn-save-token',
    'btn-identify', 'btn-reboot', 'btn-reset',
    // Network
    'btn-save-eth', 'btn-save-wifi', 'btn-save-wifi-net', 'btn-save-dns',
    // IR
    'btn-ir-send', 'btn-ir-learn',
    // Ports
    'btn-port1-mode', 'btn-port2-mode',
    'btn-port1-uart', 'btn-port2-uart',
    'btn-port1-serial-cfg', 'btn-port2-serial-cfg',
    'btn-port1-console', 'btn-port2-console',
    'btn-port1-send', 'btn-port2-send',
    'btn-port1-trig-on', 'btn-port1-trig-off', 'btn-port1-impulse',
    'btn-port2-trig-on', 'btn-port2-trig-off', 'btn-port2-impulse',
    // Logs
    'btn-log-stream',
    // Expert
    'btn-expert-apply'
  ],

  init: function () {
    const self = this;

    // Nav click handlers
    document.querySelectorAll("#main-nav a").forEach(function (a) {
      a.addEventListener("click", function (e) {
        e.preventDefault();
        self.showPage(a.dataset.page);
      });
    });

    // Login link in header
    document.getElementById("btn-login-link").addEventListener("click", function () {
      self.showLogin();
    });

    // Login handlers
    document.getElementById("login-btn").addEventListener("click", function () {
      const pw = document.getElementById("login-pw").value;
      if (pw.length >= 1) {
        WS.authenticate(pw);
      }
    });
    document.getElementById("login-pw").addEventListener("keydown", function (e) {
      if (e.key === "Enter") document.getElementById("login-btn").click();
    });

    // ESC closes login overlay
    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape" && !document.getElementById("login-overlay").classList.contains("hidden")) {
        self.hideLogin();
        self.pendingPage = null;
      }
    });

    // Logout
    document.getElementById("btn-logout").addEventListener("click", function () {
      WS.token = null;
      WS.authenticated = false;
      WS.keepConnected = false;
      sessionStorage.removeItem("token");
      if (WS.socket && WS.socket.readyState === 1) {
        WS.socket.close();
      }
      UI.setStatus("err");
      self.showPage("status");
      Toast.success(I18N.t("t_logged_out"));
    });

    // Restore token from session
    WS.token = sessionStorage.getItem("token");

    // Set connection mode based on auth state
    WS.keepConnected = !!WS.token;

    // Disable all actions until connected and authenticated
    this.setActionsEnabled(false);

    // Connect WebSocket
    WS.connect();
  },

  showPage: function (page) {
    // Tear down the current page before switching
    if (this.currentPage === "ir") {
      Pages.IR.teardown();
    }

    if (this.authRequired[page] && !WS.authenticated) {
      this.pendingPage = page;
      this.showLogin();
      return;
    }
    document.querySelectorAll("#main-nav a").forEach(function (a) {
      a.classList.toggle("active", a.dataset.page === page);
    });
    document.querySelectorAll("main > section").forEach(function (s) {
      s.classList.toggle("hidden", s.id !== "page-" + page);
    });
    this.currentPage = page;
    this.loadPage(page);
    this.updateLogoutVisibility();

    // Start/stop status refresh based on page
    if (WS.authenticated) {
      if (page === "status") {
        WS.startStatusRefresh();
        Pages.Status.load(false);  // Don't force refresh when auto-refresh is active
      } else {
        WS.stopStatusRefresh();
      }
      // Reset inactivity timer on navigation (user activity)
      WS.trackActivity();
    }
  },

  loadPage: function (page) {
    switch (page) {
      case "status":
        Pages.Status.load(true);
        break;  // Force refresh when navigating to status
      case "general":
        Pages.General.load();
        break;
      case "network":
        Pages.Network.load();
        break;
      case "ir":
        Pages.IR.load();
        break;
      case "ports":
        Pages.Ports.load();
        break;
      case "logs":
        Pages.Logs.load();
        break;
      case "ota":
        Pages.OTA.load();
        break;
      case "expert":
        Pages.Expert.load();
        break;
    }
  },

  onAuthenticated: function () {
    const page = this.pendingPage || this.currentPage;
    this.pendingPage = null;
    this.showPage(page);
    this.updateLogoutVisibility();
    WS.updateActionState();
  },

  updateLogoutVisibility: function () {
    const loginBtn = document.getElementById("btn-login-link");
    const logoutBtn = document.getElementById("btn-logout");
    if (loginBtn) loginBtn.classList.toggle("hidden", WS.authenticated);
    if (logoutBtn) logoutBtn.classList.toggle("hidden", !WS.authenticated);
  },

  setStatus: function (state) {
    const dot = document.querySelector("#conn-status .status-dot");
    const text = document.getElementById("conn-text");
    if (state === "connecting") {
      dot.className = "status-dot warn";
    } else {
      dot.className = "status-dot " + state;
    }
    text.textContent = I18N.t("st_" + (state === "connecting" ? "connecting" : (state === "ok" ? "connected" : "disconnected")));
    this.updateLogoutVisibility();
  },

  setActionsEnabled: function (enabled) {
    this.actionButtonIds.forEach(function (id) {
      const btn = document.getElementById(id);
      if (btn) {
        btn.disabled = !enabled;
      }
    });

    if (Pages.Ports) {
      Pages.Ports.updateModeApplyStates();
    }
  },

  showLogin: function (errMsg) {
    const overlay = document.getElementById("login-overlay");
    overlay.classList.remove("hidden");
    const errEl = document.getElementById("login-err");
    if (errMsg) {
      errEl.textContent = errMsg;
      errEl.classList.remove("hidden");
    } else {
      errEl.classList.add("hidden");
    }
    document.getElementById("login-pw").value = "";
    document.getElementById("login-pw").focus();
    // Open persistent connection when login dialog is shown
    WS.keepConnected = true;
    if (!WS.socket || WS.socket.readyState !== 1) {
      WS.connect();
    }
  },

  hideLogin: function () {
    document.getElementById("login-overlay").classList.add("hidden");
  },

  // Store sysinfo globally for reuse across pages
  cacheSysInfo: function (data) {
    this.sysInfoCache = data;
  },

  // Get cached sysinfo (returns null if not cached)
  getCachedSysInfo: function () {
    return this.sysInfoCache;
  }
};

// ============================================================
// Page: Status
// ============================================================
Pages.Status = {
  render: function (data) {
    document.getElementById("s-name").textContent = data.name || "\u2014";
    document.getElementById("s-hostname").textContent = data.hostname || "\u2014";
    document.getElementById("s-model").textContent = data.model || "\u2014";
    document.getElementById("s-revision").textContent = data.revision || "\u2014";
    document.getElementById("s-version").textContent = data.version || "\u2014";
    document.getElementById("s-serial").textContent = data.serial || "\u2014";
    document.getElementById("s-ethernet").textContent = data.ethernet
      ? I18N.t("st_connected") : I18N.t("st_disconnected");
    document.getElementById("s-wifi").textContent = data.wifi
      ? I18N.t("st_connected") + (data.ssid ? " (" + data.ssid + ")" : "")
      : I18N.t("st_disconnected");
    document.getElementById("s-uptime").textContent = data.uptime || "\u2014";
    const date = data.time ? new Date(data.time) : null;
    document.getElementById("s-time").textContent = date && !Number.isNaN(date.getTime()) ? date.toLocaleString() : "\u2014";
    document.getElementById("s-heap").textContent = data.free_heap || "\u2014";
    document.getElementById("s-reset").textContent = data.reset_reason || "\u2014";
  },

  load: function (forceRefresh) {
    // If not forcing refresh and we have cached data, use it
    if (!forceRefresh) {
      const cached = UI.getCachedSysInfo();
      if (cached) {
        Pages.Status.render(cached);
        return;
      }
    }

    WS.request("get_sysinfo").then(function (data) {
      // Cache the sysinfo for reuse across pages
      UI.cacheSysInfo(data);
      Pages.Status.render(data);
      // Update OTA version if OTA page is active
      if (UI.currentPage === "ota") {
        Pages.OTA.updateVersion();
      }
    }).catch(function (e) {
      Toast.error(e);
    });
  },

  loadWithClose: function () {
    WS.request("get_sysinfo").then(function (data) {
      // Cache the sysinfo for reuse across pages
      UI.cacheSysInfo(data);
      Pages.Status.render(data);
      // Close WebSocket after fetching status when not authenticated
      setTimeout(function () {
        WS.disconnect();
      }, 500);
    }).catch(function () {
      setTimeout(function () {
        WS.disconnect();
      }, 500);
    });
  }
};

// ============================================================
// Page: General
// ============================================================
Pages.General = {
  load: function () {
    WS.request("get_sysinfo").then(function (data) {
      document.getElementById("cfg-name").value = data.name || "";
      let ledPct = Math.round((data.led_brightness || 0) / 255 * 100);
      let ethPct = Math.round((data.eth_led_brightness || 0) / 255 * 100);
      document.getElementById("cfg-led").value = ledPct;
      document.getElementById("cfg-led-val").textContent = ledPct + "%";
      document.getElementById("cfg-eth-led").value = ethPct;
      document.getElementById("cfg-eth-led-val").textContent = ethPct + "%";
    }).catch(function (e) {
      Toast.error(e);
    });
  },

  init: function () {
    document.getElementById("cfg-led").addEventListener("input", function () {
      document.getElementById("cfg-led-val").textContent = this.value + "%";
    });
    document.getElementById("cfg-eth-led").addEventListener("input", function () {
      document.getElementById("cfg-eth-led-val").textContent = this.value + "%";
    });

    document.getElementById("btn-save-name").addEventListener("click", function () {
      let name = document.getElementById("cfg-name").value.trim();
      if (!name) return;
      WS.request("set_config", {friendly_name: name})
        .then(function () {
          Toast.success(I18N.t("t_name_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    document.getElementById("btn-save-brightness").addEventListener("click", function () {
      let ledPct = parseInt(document.getElementById("cfg-led").value);
      let ethPct = parseInt(document.getElementById("cfg-eth-led").value);
      WS.request("set_brightness", {
        status_led: Math.round(ledPct / 100 * 255),
        eth_led: Math.round(ethPct / 100 * 255)
      })
        .then(function () {
          Toast.success(I18N.t("t_brightness_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    document.getElementById("btn-save-token").addEventListener("click", function () {
      let t = document.getElementById("cfg-token").value;
      if (t.length < 4) {
        Toast.show(I18N.t("e_token_min"), true);
        return;
      }
      WS.request("set_config", {token: t}).then(function () {
        WS.token = t;
        sessionStorage.setItem("token", t);
        document.getElementById("cfg-token").value = "";
        Toast.success(I18N.t("t_token_changed"));
      }).catch(function (e) {
        Toast.error(e);
      });
    });

    document.getElementById("btn-identify").addEventListener("click", function () {
      WS.request("identify")
        .then(function () {
          Toast.success(I18N.t("t_identify"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    document.getElementById("btn-reboot").addEventListener("click", function () {
      if (confirm(I18N.t("confirm_reboot"))) {
        WS.request("reboot").catch(function (e) {
          console.error(e);
        });
        Toast.success(I18N.t("t_rebooting"));
      }
    });

    document.getElementById("btn-reset").addEventListener("click", function () {
      if (confirm(I18N.t("confirm_reset"))) {
        WS.request("reset").catch(function (e) {
          console.error(e);
        });
        Toast.success(I18N.t("t_reset_initiated"));
        sessionStorage.removeItem("token");
      }
    });
  }
};

// ============================================================
// Page: Network
// ============================================================
Pages.Network = {
  ethConfig: {
    mode: "dhcp",
    ip: "",
    mask: "",
    gw: ""
  },
  wifiConfig: {
    mode: "dhcp",
    ip: "",
    mask: "",
    gw: ""
  },
  dnsConfig: {
    dns1: "",
    dns2: ""
  },

  load: function () {
    const self = this;

    WS.request("get_sysinfo").then(function (data) {
      // Connection status
      document.getElementById("net-eth").textContent = data.ethernet
        ? I18N.t("st_connected") : I18N.t("st_disconnected");
      document.getElementById("net-wifi").textContent = data.wifi
        ? I18N.t("st_connected") : I18N.t("st_disconnected");
      document.getElementById("net-ssid").textContent = data.ssid || "\u2014";
      document.getElementById("cfg-ssid").value = data.ssid || "";
    }).catch(function (e) {
      Toast.error(e);
    });

    // Get network configuration
    WS.request("get_network").then(function (data) {
      // Active configuration
      if (data.active) {
        document.getElementById("net-ip").textContent = data.active.ip || "\u2014";
        document.getElementById("net-mask").textContent = data.active.mask || "\u2014";
        document.getElementById("net-gw").textContent = data.active.gw || "\u2014";

        // DNS servers
        let dnsList = [];
        if (data.active.dns1) dnsList.push(data.active.dns1);
        if (data.active.dns2) dnsList.push(data.active.dns2);
        if (data.active.dns3) dnsList.push(data.active.dns3);
        document.getElementById("net-dns").textContent = dnsList.length > 0 ? dnsList.join(", ") : "\u2014";

        // IPv6 addresses
        if (data.active.ipv6 && data.active.ipv6.addresses && data.active.ipv6.addresses.length > 0) {
          let ipv6List = data.active.ipv6.addresses.map(function (addr) {
            let typeLabel = addr.type ? " (" + addr.type.replace("_", "-") + ")" : "";
            return addr.address + typeLabel;
          });
          document.getElementById("net-ipv6").textContent = ipv6List.join(", ");
        } else {
          document.getElementById("net-ipv6").textContent = "\u2014";
        }
      }

      // Ethernet configuration
      if (data.eth) {
        self.ethConfig.mode = data.eth.mode || "dhcp";
        self.ethConfig.ip = data.eth.ip || "";
        self.ethConfig.mask = data.eth.mask || "";
        self.ethConfig.gw = data.eth.gw || "";
        document.getElementById("cfg-eth-mode").value = self.ethConfig.mode;
        document.getElementById("cfg-eth-ip").value = self.ethConfig.ip;
        document.getElementById("cfg-eth-mask").value = self.ethConfig.mask;
        document.getElementById("cfg-eth-gw").value = self.ethConfig.gw;
      }
      self.updateEthFields();

      // WiFi configuration
      if (data.wifi) {
        self.wifiConfig.mode = data.wifi.mode || "dhcp";
        self.wifiConfig.ip = data.wifi.ip || "";
        self.wifiConfig.mask = data.wifi.mask || "";
        self.wifiConfig.gw = data.wifi.gw || "";
      }
      document.getElementById("cfg-wifi-mode").value = self.wifiConfig.mode;
      document.getElementById("cfg-wifi-ip").value = self.wifiConfig.ip;
      document.getElementById("cfg-wifi-mask").value = self.wifiConfig.mask;
      document.getElementById("cfg-wifi-gw").value = self.wifiConfig.gw;
      self.updateWifiFields();

      // DNS configuration
      if (data.dns1 !== undefined) self.dnsConfig.dns1 = data.dns1;
      if (data.dns2 !== undefined) self.dnsConfig.dns2 = data.dns2;
      document.getElementById("cfg-dns1").value = self.dnsConfig.dns1;
      document.getElementById("cfg-dns2").value = self.dnsConfig.dns2;
    }).catch(function (e) {
      Toast.error(e);
    });
  },

  updateEthFields: function () {
    let isStatic = this.ethConfig.mode === "static";
    document.getElementById("cfg-eth-ip").disabled = !isStatic;
    document.getElementById("cfg-eth-mask").disabled = !isStatic;
    document.getElementById("cfg-eth-gw").disabled = !isStatic;
  },

  updateWifiFields: function () {
    let isStatic = this.wifiConfig.mode === "static";
    document.getElementById("cfg-wifi-ip").disabled = !isStatic;
    document.getElementById("cfg-wifi-mask").disabled = !isStatic;
    document.getElementById("cfg-wifi-gw").disabled = !isStatic;
  },

  validateIp: function (ip) {
    if (!ip) return true; // empty is valid (will be ignored)
    let pattern = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/;
    let match = ip.match(pattern);
    if (!match) return false;
    for (let i = 1; i <= 4; i++) {
      if (parseInt(match[i]) > 255) return false;
    }
    return true;
  },

  init: function () {
    const self = this;

    // Ethernet mode change
    document.getElementById("cfg-eth-mode").addEventListener("change", function () {
      self.ethConfig.mode = this.value;
      self.updateEthFields();
    });

    // Ethernet save
    document.getElementById("btn-save-eth").addEventListener("click", function () {
      let ip = document.getElementById("cfg-eth-ip").value.trim();
      let mask = document.getElementById("cfg-eth-mask").value.trim();
      let gw = document.getElementById("cfg-eth-gw").value.trim();

      if (!self.validateIp(ip)) {
        Toast.show(I18N.t("e_invalid_ip"), true);
        return;
      }
      if (!self.validateIp(mask)) {
        Toast.show(I18N.t("e_invalid_ip"), true);
        return;
      }
      if (!self.validateIp(gw)) {
        Toast.show(I18N.t("e_invalid_ip"), true);
        return;
      }

      let data = {
        interface: "eth",
        mode: self.ethConfig.mode
      };
      if (self.ethConfig.mode === "static") {
        data.ip = ip;
        data.mask = mask;
        data.gw = gw;
      }

      WS.request("set_network", data)
        .then(function () {
          Toast.success(I18N.t("t_network_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    // WiFi mode change
    document.getElementById("cfg-wifi-mode").addEventListener("change", function () {
      self.wifiConfig.mode = this.value;
      self.updateWifiFields();
    });

    // WiFi save
    document.getElementById("btn-save-wifi").addEventListener("click", function () {
      let ssid = document.getElementById("cfg-ssid").value.trim();
      let pw = document.getElementById("cfg-wifi-pw").value;

      if (!ssid) {
        Toast.show(I18N.t("e_ssid_required"), true);
        return;
      }
      if (!pw) {
        Toast.show(I18N.t("e_pw_required"), true);
        return;
      }

      WS.request("set_config", {ssid: ssid, wifi_password: pw})
        .then(function () {
          Toast.success(I18N.t("t_wifi_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    // WiFi network save
    document.getElementById("btn-save-wifi-net").addEventListener("click", function () {
      let ip = document.getElementById("cfg-wifi-ip").value.trim();
      let mask = document.getElementById("cfg-wifi-mask").value.trim();
      let gw = document.getElementById("cfg-wifi-gw").value.trim();

      if (!self.validateIp(ip)) {
        Toast.show(I18N.t("e_invalid_ip"), true);
        return;
      }
      if (!self.validateIp(mask)) {
        Toast.show(I18N.t("e_invalid_ip"), true);
        return;
      }
      if (!self.validateIp(gw)) {
        Toast.show(I18N.t("e_invalid_ip"), true);
        return;
      }

      const data = self.wifiConfig.mode === "static" ?
        {
          interface: "wifi",
          mode: "static",
          ip: ip,
          mask: mask,
          gw: gw
        }
        : {
          interface: "wifi",
          mode: "dhcp"
        };

      WS.request("set_network", data)
        .then(function () {
          Toast.success(I18N.t("t_network_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    // Password reveal toggle
    document.getElementById("btn-reveal-wifi").addEventListener("click", function () {
      let input = document.getElementById("cfg-wifi-pw");
      let show = input.type === "password";
      input.type = show ? "text" : "password";
      this.textContent = show ? I18N.t("btn_hide") : I18N.t("btn_show");
    });

    // DNS save
    document.getElementById("btn-save-dns").addEventListener("click", function () {
      let dns1 = document.getElementById("cfg-dns1").value.trim();
      let dns2 = document.getElementById("cfg-dns2").value.trim();

      if (dns1 && !self.validateIp(dns1)) {
        Toast.show(I18N.t("e_invalid_ip"), true);
        return;
      }
      if (dns2 && !self.validateIp(dns2)) {
        Toast.show(I18N.t("e_invalid_ip"), true);
        return;
      }

      let data = {};
      if (dns1) data.dns1 = dns1;
      if (dns2) data.dns2 = dns2;

      WS.request("set_dns", data)
        .then(function () {
          Toast.success(I18N.t("t_dns_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });
  }
};


// ============================================================
// Page: IR
// ============================================================
Pages.IR = {
  learning: false,
  repeating: false,
  repeatTimer: null,
  portModes: {1: null, 2: null},
  _mouseDown: false,
  _savedHoldValue: "0",
  rawMode: false,
  overflowWarning: false,

  load: function () {
    const self = this;
    // Get port modes to determine checkbox availability
    WS.request("get_port_modes").then(function (data) {
      if (data.ports) {
        data.ports.forEach(function (p) {
          self.portModes[p.port] = p.active_mode || p.mode;
        });
      }
      self.updateOutputCheckboxes();
    }).catch(function (e) {
      Toast.error(e);
    });

    // Get sysinfo for ir_learning state
    WS.request("get_sysinfo").then(function (data) {
      self.learning = data.ir_learning || false;
      self.updateLearnButton();
      self.updateSendButtonState();
    }).catch(function (e) {
      Toast.error(e);
    });
  },

  isIrOutput: function (mode) {
    return mode === "IR_BLASTER" || mode === "IR_EMITTER_MONO_PLUG" || mode === "IR_EMITTER_STEREO_PLUG";
  },

  updateOutputCheckboxes: function () {
    const ext1 = document.getElementById("ir-ext1");
    const ext2 = document.getElementById("ir-ext2");
    const port1IsIr = this.isIrOutput(this.portModes[1]);
    const port2IsIr = this.isIrOutput(this.portModes[2]);

    ext1.disabled = !port1IsIr;
    ext2.disabled = !port2IsIr;
    if (!port1IsIr) ext1.checked = false;
    if (!port2IsIr) ext2.checked = false;

    document.getElementById("ir-ext1-label").classList.toggle("disabled", !port1IsIr);
    document.getElementById("ir-ext2-label").classList.toggle("disabled", !port2IsIr);
  },


  updateLearnButton: function () {
    const btn = document.getElementById("btn-ir-learn");
    btn.textContent = this.learning ? I18N.t("btn_stop_learning") : I18N.t("btn_start_learning");
    btn.classList.toggle("btn-warn", this.learning);
    // Disable raw mode checkbox when learning is active
    const rawCheckbox = document.getElementById("ir-raw-mode");
    rawCheckbox.disabled = this.learning;
  },

  updateRawPanelVisibility: function () {
    const rawPanel = document.getElementById("ir-raw-panel");
    const rawCheckbox = document.getElementById("ir-raw-mode");
    rawPanel.classList.toggle("hidden", !rawCheckbox.checked);
  },

  updateOverflowWarning: function (overflow) {
    const warning = document.getElementById("ir-overflow-warning");
    this.overflowWarning = overflow === true;
    warning.classList.toggle("hidden", !this.overflowWarning);
  },

  updateSendButtonState: function () {
    const btn = document.getElementById("btn-ir-send");
    const repeatMode = document.getElementById("ir-repeat-mode").checked;
    const code = document.getElementById("ir-code").value.trim();

    // Disable button if learning is active
    if (this.learning) {
      btn.disabled = true;
      return;
    }

    // Disable button if no code entered
    if (!code) {
      btn.disabled = true;
      return;
    }

    // Button is enabled if:
    // - Normal mode (repeat mode unchecked), or
    // - Repeat mode checked AND repeat value > 0
    const repeatValue = parseInt(document.getElementById("ir-repeat").value) || 0;
    btn.disabled = !!(repeatMode && repeatValue <= 0);
  },


  updateFormatFields: function () {
    const format = document.getElementById("ir-format").value;
    const freqInput = document.getElementById("ir-freq");
    const freqGroup = document.getElementById("ir-freq-group");

    // Show/hide frequency field based on format
    freqGroup.classList.toggle("hidden", format !== "raw");
    freqInput.disabled = format !== "raw";

    // Check RAW warning conditions
    this.updateRawWarning();
  },

  updateRawWarning: function () {
    const format = document.getElementById("ir-format").value;
    const code = document.getElementById("ir-code").value.trim();
    const repeat = parseInt(document.getElementById("ir-repeat").value) || 0;
    const hold = parseInt(document.getElementById("ir-hold").value) || 0;
    const warning = document.getElementById("ir-raw-warning");

    // Only show warning for RAW format with repeat > 0 OR hold > 0
    if (format !== "raw" || (repeat <= 0 && hold <= 0)) {
      warning.classList.add("hidden");
      return;
    }

    // Count values in the code (split by whitespace)
    const cleanedCode = code.replace(/\u00A0/g, " ").replace(/  +/g, " ").trim();
    if (!cleanedCode) {
      warning.classList.add("hidden");
      return;
    }

    const values = cleanedCode.split(" ");
    const isOdd = values.length % 2 !== 0;

    warning.classList.toggle("hidden", !isOdd);
  },

  updateRepeatModeCheckbox: function () {
    const checkbox = document.getElementById("ir-repeat-mode");
    const repeatInput = document.getElementById("ir-repeat");
    const repeatValue = parseInt(repeatInput.value) || 0;

    if (repeatValue <= 0) {
      // Repeat cleared: always disable and uncheck the checkbox,
      // then sync dependent fields in case it was previously checked.
      if (checkbox.checked) {
        checkbox.checked = false;
        this.toggleRepeatModeFields();
      }
    }

    this.updateSendButtonState();
  },

  toggleRepeatModeFields: function () {
    const repeatMode = document.getElementById("ir-repeat-mode").checked;
    const holdInput = document.getElementById("ir-hold");
    const periodicInput = document.getElementById("ir-interval");
    const repeatInput = document.getElementById("ir-repeat");
    const repeatValue = parseInt(repeatInput.value) || 0;

    holdInput.disabled = repeatMode;
    if (repeatMode) {
      this._savedHoldValue = holdInput.value;
      holdInput.value = "0";
      if (repeatValue <= 0) {
        repeatInput.value = "6";
      }
    } else {
      holdInput.value = this._savedHoldValue;
    }

    // Enable periodic field only if repeat mode is enabled
    periodicInput.disabled = !repeatMode;

    this.updateSendButtonState();
    this.updateSendButtonVisual();
  },

  startRepeat: function () {
    const self = this;
    const code = document.getElementById("ir-code").value.trim();
    if (!code) {
      Toast.show(I18N.t("e_ir_required"), true);
      this._resetRepeatState();
      return;
    }

    const format = document.getElementById("ir-format").value;
    const repeat = parseInt(document.getElementById("ir-repeat").value) || 6;
    const periodic = parseInt(document.getElementById("ir-interval").value) || 300;

    // Clamp periodic to valid range
    let clampedPeriodic = periodic;
    if (periodic < 100) clampedPeriodic = 100;
    if (periodic > 1000) clampedPeriodic = 1000;

    const data = {
      type: "dock",
      command: "ir_send",
      code: code,
      format: format,
      repeat: repeat,
      f: 1,  // Disable ack responses for repeat extensions
      int_side: document.getElementById("ir-int-side").checked,
      int_top: false,
      ext1: document.getElementById("ir-ext1").checked,
      ext2: document.getElementById("ir-ext2").checked
    };

    // Process code for RAW format
    if (format === "raw") {
      const freq = parseInt(document.getElementById("ir-freq").value) || 38000;
      const cleanedCode = code.replace(/\u00A0/g, " ").replace(/  +/g, " ").trim();
      data.code = freq + ":" + cleanedCode;
    }

    // Ensure state is reset before starting
    this._resetRepeatState();

    // Set repeating immediately so a button-up before the first
    // response arrives is handled correctly by handleSendButtonUp.
    this.repeating = true;
    this.updateSendButtonVisual();

    // Send initial ir_send with request tracking
    WS.request("ir_send", data)
      .then(function () {
        // Only start the interval if still repeating (user may have
        // released the button before the first response arrived).
        if (!self.repeating) return;

        // Start periodic sending - use WS.send directly to avoid timeout
        // (f:1 suppresses responses, so request would timeout after 10s)
        self.repeatTimer = setInterval(function () {
          // Re-send to extend repeat using direct send (no response tracking)
          try {
            WS.send(data);
          } catch (e) {
            // Connection error, stop repeat
            self._resetRepeatState();
            Toast.error(e);
          }
        }, clampedPeriodic);
      })
      .catch(function (e) {
        self._resetRepeatState();
        Toast.error(e);
      });
  },

  stopRepeat: function () {
    const self = this;

    // Stop periodic timer first
    if (this.repeatTimer) {
      clearInterval(this.repeatTimer);
      this.repeatTimer = null;
    }

    // Reset repeating state immediately to prevent new messages
    this.repeating = false;
    this.updateSendButtonVisual();

    // Send ir_stop to dock
    WS.request("ir_stop")
      .then(function () {
        // State already reset, just ensure visual is correct
        self.updateSendButtonVisual();
      })
      .catch(function (e) {
        // State already reset, just show error
        Toast.error(e);
      });
  },

  teardown: function () {
    if (this.repeating) {
      this.stopRepeat();
    }
    this._mouseDown = false;
  },

  _resetRepeatState: function () {
    // Internal helper to reset all repeat-related state
    if (this.repeatTimer) {
      clearInterval(this.repeatTimer);
      this.repeatTimer = null;
    }
    this.repeating = false;
    this.updateSendButtonVisual();
  },

  updateSendButtonVisual: function () {
    const btn = document.getElementById("btn-ir-send");
    const repeatMode = document.getElementById("ir-repeat-mode").checked;

    if (this.repeating) {
      btn.textContent = I18N.t("btn_sending");
      btn.classList.add("btn-send-active", "btn-warn");
    } else {
      btn.textContent = repeatMode ? I18N.t("btn_start_repeat") : I18N.t("btn_send");
      btn.classList.remove("btn-send-active", "btn-warn");
    }
  },

  sendSingle: function () {
    const code = document.getElementById("ir-code").value.trim();
    if (!code) {
      Toast.show(I18N.t("e_ir_required"), true);
      return;
    }

    const format = document.getElementById("ir-format").value;
    const repeat = parseInt(document.getElementById("ir-repeat").value) || 0;
    const hold = parseInt(document.getElementById("ir-hold").value) || 0;

    const data = {
      code: code,
      format: format,
      int_side: document.getElementById("ir-int-side").checked,
      int_top: false,
      ext1: document.getElementById("ir-ext1").checked,
      ext2: document.getElementById("ir-ext2").checked
    };

    // Process code for RAW format
    if (format === "raw") {
      const freq = parseInt(document.getElementById("ir-freq").value) || 38000;
      // Clean whitespace: replace multiple spaces/non-breaking spaces with single space, trim
      const cleanedCode = code.replace(/\u00A0/g, " ").replace(/  +/g, " ").trim();
      data.code = freq + ":" + cleanedCode;
    }

    // Add repeat or hold based on values
    // hold overrides repeat if both are set
    if (hold > 0) {
      data.hold = hold;
    } else if (repeat > 0) {
      data.repeat = repeat;
    }

    WS.request("ir_send", data)
      .then(function () {
        Toast.success(I18N.t("t_ir_sent"));
      })
      .catch(function (e) {
        Toast.error(e);
      });
  },

  handleSendButtonDown: function (e) {
    // Prevent default to avoid text selection on mobile
    e.preventDefault();
    this._mouseDown = true;

    const repeatMode = document.getElementById("ir-repeat-mode").checked;

    if (repeatMode && !this.repeating) {
      this.startRepeat();
    }
    // In normal mode, single click sends once (handled by click event)
  },


  handleSendButtonUp: function (e) {
    e.preventDefault();
    this._mouseDown = false;

    const repeatMode = document.getElementById("ir-repeat-mode").checked;

    // Call stopRepeat unconditionally in repeat mode: it is idempotent
    // (timer check inside) and ir_stop is safe to send when idle.
    // This covers the race where an error clears repeating before mouseup.
    if (repeatMode) {
      this.stopRepeat();
    }
  },

  copyRawToClipboard: function () {
    const self = this;
    const output = document.getElementById("ir-raw-output");
    if (!output || !output.textContent) {
      Toast.show(I18N.t("e_no_content"), true);
      return;
    }

    // Replace non-breaking spaces with regular spaces, collapse multiple spaces, trim
    const text = output.textContent
      .replace(/\u00A0/g, " ")
      .replace(/  +/g, " ")
      .trim();

    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function () {
        Toast.success(I18N.t("t_copied"));
      }).catch(function () {
        self.fallbackCopy(text);
      });
    } else {
      this.fallbackCopy(text);
    }
  },

  fallbackCopy: function (text) {
    const textarea = document.createElement("textarea");
    textarea.value = text;
    textarea.style.position = "fixed";
    textarea.style.left = "-9999px";
    document.body.appendChild(textarea);
    textarea.select();
    try {
      document.execCommand("copy");
      Toast.success(I18N.t("t_copied"));
    } catch (err) {
      Toast.error(err);
    }
    document.body.removeChild(textarea);
  },

  formatRawTimings: function (rawArray) {
    const minFieldWidth = 6;
    const nbsp = "\u00A0"; // Non-breaking space
    const paddedValues = rawArray.map(function (value) {
      const str = String(value);
      const padding = minFieldWidth - str.length;
      return nbsp.repeat(padding) + str;
    });
    return paddedValues.join(" ");
  },


  init: function () {
    const self = this;

    // IR code field change - enable/disable send button and check warning
    document.getElementById("ir-code").addEventListener("input", function () {
      self.updateSendButtonState();
      self.updateRawWarning();
    });

    // Format change - show/hide frequency field and check warning
    document.getElementById("ir-format").addEventListener("change", function () {
      self.updateFormatFields();
      self.updateSendButtonState();
    });

    // Repeat value change - enables/disables repeat mode checkbox and check warning
    document.getElementById("ir-repeat").addEventListener("input", function () {
      self.updateRepeatModeCheckbox();
      self.updateRawWarning();
    });
    document.getElementById("ir-hold").addEventListener("input", function () {
      self.updateRawWarning();
    });

    // Repeat mode checkbox toggle - set repeat count if 0, disable hold time
    document.getElementById("ir-repeat-mode").addEventListener("change", function () {
      self.toggleRepeatModeFields();
      self.updateRawWarning();
    });

    // RAW mode checkbox toggle
    document.getElementById("ir-raw-mode").addEventListener("change", function () {
      self.rawMode = this.checked;
      self.updateRawPanelVisibility();
    });

    // Copy raw button
    document.getElementById("btn-ir-raw-copy").addEventListener("click", function () {
      self.copyRawToClipboard();
    });

    // Send button - mouse events for desktop
    const sendBtn = document.getElementById("btn-ir-send");
    sendBtn.addEventListener("mousedown", function (e) {
      self.handleSendButtonDown(e);
    });
    sendBtn.addEventListener("mouseup", function (e) {
      self.handleSendButtonUp(e);
    });
    sendBtn.addEventListener("mouseleave", function (e) {
      // Only stop repeat if the mouse button is actually held down
      if (self._mouseDown) {
        self.handleSendButtonUp(e);
      }
    });

    // Send button - touch events for mobile
    sendBtn.addEventListener("touchstart", function (e) {
      self.handleSendButtonDown(e);
    });
    sendBtn.addEventListener("touchend", function (e) {
      self.handleSendButtonUp(e);
    });
    sendBtn.addEventListener("touchcancel", function (e) {
      self.handleSendButtonUp(e);
    });

    // Send button - click for normal mode (single send)
    sendBtn.addEventListener("click", function (e) {
      const repeatMode = document.getElementById("ir-repeat-mode").checked;
      if (repeatMode) {
        // Suppress click in repeat mode — mousedown already started the hold
        e.preventDefault();
      } else {
        self.sendSingle();
      }
    });

    // Learn IR toggle
    document.getElementById("btn-ir-learn").addEventListener("click", function () {
      const command = self.learning ? "ir_receive_off" : "ir_receive_on";
      const data = {};
      // Only send format: "raw" when enabling learning AND raw mode is checked
      if (!self.learning && self.rawMode) {
        data.raw = true;
      }
      WS.request(command, data).then(function () {
        self.learning = !self.learning;
        self.updateLearnButton();
        self.updateSendButtonState();
        Toast.success(self.learning ? I18N.t("t_ir_learn_on") : I18N.t("t_ir_learn_off"));
      }).catch(function (e) {
        Toast.error(e);
      });
    });

    // Clear learned codes
    document.getElementById("btn-ir-clear").addEventListener("click", function () {
      document.getElementById("ir-learn-output").textContent = "";
      document.getElementById("ir-raw-output").textContent = "";
      self.updateOverflowWarning(false);
    });

    // Listen for ir_receive events
    WS.on("ir_receive", function (msg) {
      const output = document.getElementById("ir-learn-output");
      const rawOutput = document.getElementById("ir-raw-output");
      if (!output) return;
      if (output.textContent) output.textContent += "\n";
      output.textContent += msg.ir_code;
      const container = output.parentElement;
      container.scrollTop = container.scrollHeight;

      // Handle raw timings if present
      if (rawOutput && msg.raw && msg.raw.length > 0) {
        self.rawMode = true;
        document.getElementById("ir-raw-mode").checked = self.rawMode;
        self.updateRawPanelVisibility();
        rawOutput.textContent = Pages.IR.formatRawTimings(msg.raw);
      }

      // Handle overflow warning
      self.updateOverflowWarning(msg.overflow);
    });

    // Listen for ir_receive_on/off events (Dock 3)
    WS.on("ir_receive_on", function (msg) {
      self.learning = true;
      self.updateLearnButton();
      self.updateSendButtonState();
      // Check if raw mode was enabled (possibly by another client)
      if (msg.raw === true) {
        const rawCheckbox = document.getElementById("ir-raw-mode");
        rawCheckbox.checked = true;
        self.rawMode = true;
        self.updateRawPanelVisibility();
      }
    });

    WS.on("ir_receive_off", function (msg) {
      self.learning = false;
      self.updateLearnButton();
      self.updateSendButtonState();
    });

    window.addEventListener("beforeunload", function () {
      self.teardown();
    });

    window.addEventListener("blur", function () {
      self.teardown();
    });
  }
};

// ============================================================
// RS232 Console Data Encoding
// ============================================================

/**
 * Encodes non-printable characters as \xHH escape sequences
 * and converts line breaks to actual newlines for display
 * @param {string} data - Received data string
 * @param {string|null} terminatorChar - Custom terminator character (Line mode only)
 * @param {string} bufferingMode - 'line' or 'chunk'
 * @returns {string} Encoded display string
 */
function encodeConsoleData(data, terminatorChar, bufferingMode) {
  let result = '';
  const isLineMode = bufferingMode === 'line';

  for (let i = 0; i < data.length; i++) {
    const code = data.charCodeAt(i);

    // Check for CRLF first (handle as single break)
    if (code === 0x0D && i + 1 < data.length && data.charCodeAt(i + 1) === 0x0A) {
      result += '\n';
      i++; // Skip the LF
      continue;
    }

    // Check for standard line breaks (single-byte only)
    if (code === 0x0A || code === 0x0D || code === 0x0C || code === 0x85) {
      result += '\n';
      continue;
    }

    // Check for custom terminator (Line mode only)
    const isTerminator = isLineMode && terminatorChar && data[i] === terminatorChar;
    if (isTerminator) {
      // If printable, display it first
      if (code >= 0x20 && code <= 0x7E) {
        result += data[i];
      }
      result += '\n';
      continue;
    }

    // Check if printable (0x20-0x7E)
    if (code >= 0x20 && code <= 0x7E) {
      // Escape backslash
      if (code === 0x5C) {
        result += '\\\\';
      } else {
        result += data[i];
      }
    } else {
      // Encode as \xHH (including extended ASCII 0x80-0xFF)
      result += '\\x' + code.toString(16).toUpperCase().padStart(2, '0');
    }
  }
  return result;
}

/**
 * Parses escape sequences in input string
 * @param {string} input - User input string
 * @returns {{success: boolean, result: string, error: {pos: number, reason: string}|null}}
 */
function parseInputEscapes(input) {
  let result = '';
  let i = 0;

  while (i < input.length) {
    if (input[i] === '\\') {
      // Check for end of string
      if (i + 1 >= input.length) {
        return {
          success: false,
          result: '',
          error: {pos: i, reason: I18N.t("e_incomplete_escape")}
        };
      }

      const nextChar = input[i + 1];

      // Handle \\ (literal backslash)
      if (nextChar === '\\') {
        result += '\\';
        i += 2;
        continue;
      }

      // Handle \xHH (hex byte)
      if (nextChar === 'x' || nextChar === 'X') {
        if (i + 3 >= input.length) {
          return {
            success: false,
            result: '',
            error: {pos: i, reason: I18N.t("e_incomplete_hex")}
          };
        }
        const hex = input.substring(i + 2, i + 4);
        if (!/^[0-9A-Fa-f]{2}$/.test(hex)) {
          return {
            success: false,
            result: '',
            error: {pos: i, reason: I18N.t("e_invalid_hex", {chars: hex})}
          };
        }
        result += String.fromCharCode(parseInt(hex, 16));
        i += 4;
        continue;
      }

      // Handle \r, \n, \t
      if (nextChar === 'r') {
        result += '\r';
        i += 2;
        continue;
      }
      if (nextChar === 'n') {
        result += '\n';
        i += 2;
        continue;
      }
      if (nextChar === 't') {
        result += '\t';
        i += 2;
        continue;
      }

      // Unknown escape sequence
      return {
        success: false,
        result: '',
        error: {pos: i, reason: I18N.t("e_unknown_escape", {seq: '\\' + nextChar})}
      };
    }

    // Regular character
    result += input[i];
    i++;
  }

  return {success: true, result: result, error: null};
}

// ============================================================
// Page: Ports
// ============================================================
Pages.Ports = {
  serialEnabled: {1: false, 2: false},
  supportedModes: {1: [], 2: []},

  // Map mode IDs to friendly names
  modeNames: {
    "AUTO": "AUTO",
    "NONE": "NONE",
    "IR_BLASTER": "IR Blaster",
    "IR_EMITTER_MONO_PLUG": "IR Emitter (Mono)",
    "IR_EMITTER_STEREO_PLUG": "IR Emitter (Stereo)",
    "TRIGGER_5V": "Trigger 5V",
    "RS232": "RS232"
  },

  load: function () {
    const self = this;
    const cached = UI.getCachedSysInfo();

    if (cached && cached.ports) {
      self.renderPortsFromSysInfo(cached.ports);
      self.loadPortModes();
      return;
    }

    WS.request("get_sysinfo").then(function (data) {
      UI.cacheSysInfo(data);
      if (data.ports) {
        self.renderPortsFromSysInfo(data.ports);
      }
      self.loadPortModes();
    }).catch(function (e) {
      Toast.error(e);
      self.renderPortsFromSysInfo([]);
      self.loadPortModes();
    });
  },

  loadPortModes: function () {
    const self = this;
    WS.request("get_port_modes").then(function (data) {
      if (data.ports) {
        data.ports.forEach(function (p) {
          self.renderPort(p);
        });
      }
    }).catch(function (e) {
      Toast.error(e);
    });
  },

  renderPortsFromSysInfo: function (ports) {
    const found = {1: false, 2: false};

    if (ports && ports.length) {
      ports.forEach(function (p) {
        if (p.port === 1 || p.port === 2) {
          found[p.port] = true;
          Pages.Ports.renderPort(p);
        }
      });
    }

    for (let n = 1; n <= 2; n++) {
      if (!found[n]) {
        this.buildModeSelect(n, null, null);
      }
    }
  },

  buildModeSelect: function (port, supportedModes, selectedMode) {
    const select = document.getElementById("port" + port + "-mode");
    if (!select) return;

    select.innerHTML = "";
    this.supportedModes[port] = supportedModes || [];

    if (!supportedModes || !supportedModes.length) {
      this.updateModeApplyState(port);
      return;
    }

    supportedModes.forEach(function (mode) {
      const opt = document.createElement("option");
      opt.value = mode;
      opt.textContent = Pages.Ports.getModeFriendlyName(mode);
      select.appendChild(opt);
    });

    if (selectedMode && supportedModes.indexOf(selectedMode) !== -1) {
      select.value = selectedMode;
      this.updateModeApplyState(port);
      return;
    }

    if (selectedMode) {
      const opt = document.createElement("option");
      opt.value = selectedMode;
      opt.textContent = "?";
      select.appendChild(opt);
      select.value = selectedMode;
    }

    this.updateModeApplyState(port);
  },

  isModeSupportedForPort: function (port, mode) {
    const modes = this.supportedModes[port];
    return !!(mode && modes && modes.indexOf(mode) !== -1);
  },

  updateModeApplyState: function (port) {
    const select = document.getElementById("port" + port + "-mode");
    const applyBtn = document.getElementById("btn-port" + port + "-mode");
    if (!applyBtn) return;

    const mode = select ? select.value : "";
    applyBtn.disabled = !WS.actionsEnabled || !this.isModeSupportedForPort(port, mode);
  },

  updateModeApplyStates: function () {
    this.updateModeApplyState(1);
    this.updateModeApplyState(2);
  },

  renderPort: function (portData) {
    const n = portData.port;
    const modeSelect = document.getElementById("port" + n + "-mode");

    if (portData.supported_modes) {
      this.buildModeSelect(n, portData.supported_modes, portData.mode);
    } else if (modeSelect && portData.mode && modeSelect.options.length > 0) {
      modeSelect.value = portData.mode;
    }

    // Show/hide Active row based on mode
    const activeRow = document.getElementById("port" + n + "-active-row");
    if (activeRow) {
      activeRow.classList.toggle("hidden", portData.mode !== "AUTO");
    }

    // Show active mode with friendly name
    const activeEl = document.getElementById("port" + n + "-active");
    if (activeEl) {
      // Use active_mode if provided, otherwise use mode
      const displayMode = portData.active_mode || portData.mode;
      activeEl.textContent = this.getModeFriendlyName(displayMode);
    }

    const activeMode = portData.active_mode || portData.mode;
    const isTrigger = activeMode === "TRIGGER_5V";
    const isRS232 = activeMode === "RS232";

    const triggerEl = document.getElementById("port" + n + "-trigger");
    const rs232El = document.getElementById("port" + n + "-rs232");
    if (triggerEl) triggerEl.classList.toggle("hidden", !isTrigger);
    if (rs232El) rs232El.classList.toggle("hidden", !isRS232);

    if (isRS232) {
      this.loadSerialConfig(n);
    }
  },

  renderPortActiveMode: function (port, mode, activeMode) {
    const n = port;

    if (mode !== undefined) {
      const activeRow = document.getElementById("port" + n + "-active-row");
      if (activeRow) {
        activeRow.classList.toggle("hidden", mode !== "AUTO");
      }
    }

    // Show active mode with friendly name (use active_mode if available, otherwise mode)
    const activeEl = document.getElementById("port" + n + "-active");
    if (activeEl) {
      const displayMode = activeMode || mode;
      activeEl.textContent = this.getModeFriendlyName(displayMode);
    }
  },

  getModeFriendlyName: function (modeId) {
    return this.modeNames[modeId] || modeId;
  },

  renderPortAfterModeChange: function (port, newMode) {
    // Update the UI immediately based on the new mode
    const n = port;
    const modeSelect = document.getElementById("port" + n + "-mode");
    if (modeSelect && modeSelect.options.length > 0) modeSelect.value = newMode;

    // Show/hide Active row based on mode
    const activeRow = document.getElementById("port" + n + "-active-row");
    if (activeRow) {
      activeRow.classList.toggle("hidden", newMode !== "AUTO");
    }

    // DO NOT update active text here - it will be set by port_mode event, or get_port_modes
    // Only update panel visibility based on mode
    const isTrigger = newMode === "TRIGGER_5V";
    const isRS232 = newMode === "RS232";

    const triggerEl = document.getElementById("port" + n + "-trigger");
    const rs232El = document.getElementById("port" + n + "-rs232");
    if (triggerEl) triggerEl.classList.toggle("hidden", !isTrigger);
    if (rs232El) rs232El.classList.toggle("hidden", !isRS232);

    if (isRS232) {
      this.loadSerialConfig(n);
    }
  },

  loadSerialConfig: function (port) {
    WS.request("get_serial_config", {port: port}).then(function (cfg) {
      const p = port;
      const uart = cfg.uart;
      if (uart) {
        if (uart.baud_rate !== undefined) document.getElementById("port" + p + "-baud").value = uart.baud_rate;
        if (uart.data_bits !== undefined) document.getElementById("port" + p + "-databits").value = uart.data_bits;
        if (uart.stop_bits !== undefined) document.getElementById("port" + p + "-stopbits").value = uart.stop_bits;
        if (uart.parity) document.getElementById("port" + p + "-parity").value = uart.parity;
      }

      if (cfg.buffering) document.getElementById("port" + p + "-buffering").value = cfg.buffering;
      if (cfg.terminator !== undefined) {
        document.getElementById("port" + p + "-terminator").value = Pages.Ports.charToHex(cfg.terminator);
      }
      if (cfg.timeout_ms !== undefined) {
        document.getElementById("port" + p + "-timeout").value = cfg.timeout_ms;
      }
    }).catch(function (e) {
      Toast.error(e);
    });
  },

  applyBuffering: function (port) {
    const hex = document.getElementById("port" + port + "-terminator").value;

    // Validate hex terminator format
    if (!this.validateHexTerminator(hex)) {
      Toast.show(I18N.t("e_invalid_terminator"), true);
      return;
    }

    const char = this.hexToChar(hex);
    if (char === null) {
      Toast.show(I18N.t("e_invalid_terminator"), true);
      return;
    }

    const timeout = parseInt(document.getElementById("port" + port + "-timeout").value) || 0;
    WS.request("set_serial_config", {
      port: port,
      buffering: document.getElementById("port" + port + "-buffering").value,
      terminator: char,
      timeout_ms: timeout
    })
      .then(function () {
        Toast.success(I18N.t("t_buffering_applied"));
      })
      .catch(function (e) {
        Toast.error(e);
      });
  },

  validateHexTerminator: function (hex) {
    const pattern = /^0x[0-9A-Fa-f]{1,2}$/;
    return pattern.test(hex.trim());
  },

  charToHex: function (str) {
    if (!str || str.length === 0) return "0x0A";
    return "0x" + str.charCodeAt(0).toString(16).toUpperCase().padStart(2, "0");
  },

  hexToChar: function (hex) {
    if (!this.validateHexTerminator(hex)) {
      return null;
    }
    const s = hex.trim().replace(/^0x/i, "");
    const code = parseInt(s, 16);
    if (isNaN(code) || code < 0 || code > 0xFF) return null;
    return String.fromCharCode(code);
  },

  getTerminatorChar: function (port) {
    const hex = document.getElementById("port" + port + "-terminator").value;
    return this.hexToChar(hex);
  },

  sendSerial: function (port) {
    const input = document.getElementById("port" + port + "-input");
    let data = input.value;
    if (!data) return;

    // Parse escape sequences
    const parsed = parseInputEscapes(data);
    if (!parsed.success) {
      Toast.show(I18N.t("e_invalid_escape_pos", {pos: parsed.error.pos, reason: parsed.error.reason}), true);
      return;
    }
    data = parsed.result;

    // Append terminator if buffering mode is "line"
    const buffering = document.getElementById("port" + port + "-buffering").value;
    if (buffering === "line") {
      data += Pages.Ports.getTerminatorChar(port);
    }

    WS.request("send_serial", {port: port, data: data})
      .catch(function (e) {
        Toast.error(e);
      });
    input.value = "";
  },

  appendConsole: function (port, data) {
    const output = document.getElementById("port" + port + "-output");
    if (!output) return;

    const buffering = document.getElementById("port" + port + "-buffering").value;
    const terminatorChar = Pages.Ports.getTerminatorChar(port);
    const encoded = encodeConsoleData(data, terminatorChar, buffering);

    output.textContent += encoded;
    const container = output.parentElement;
    container.scrollTop = container.scrollHeight;
    if (output.textContent.length > 32768) {
      output.textContent = output.textContent.slice(-31744);
    }
  },

  getUartConfig: function (n) {
    return {
      baud_rate: parseInt(document.getElementById("port" + n + "-baud").value),
      data_bits: parseInt(document.getElementById("port" + n + "-databits").value),
      stop_bits: document.getElementById("port" + n + "-stopbits").value,
      parity: document.getElementById("port" + n + "-parity").value
    };
  },

  initPort: function (n) {
    const self = this;

    document.getElementById("port" + n + "-mode").addEventListener("change", function () {
      self.updateModeApplyState(n);
    });

    document.getElementById("btn-port" + n + "-mode").addEventListener("click", function () {
      const select = document.getElementById("port" + n + "-mode");
      if (!select || !select.value || this.disabled) return;

      const mode = select.value;
      const data = {port: n, mode: mode};

      if (mode === "RS232") {
        data.uart = self.getUartConfig(n);
      }

      WS.request("set_port_mode", data).then(function () {
        Toast.success(I18N.t("t_port_mode", {port: n, mode: mode}));
        // Re-render the port card immediately to show mode-specific panels
        self.renderPortAfterModeChange(n, mode);
      }).catch(function (e) {
        Toast.error(e);
      });
    });

    document.getElementById("btn-port" + n + "-trig-on").addEventListener("click", function () {
      WS.request("set_port_trigger", {port: n, trigger: true})
        .then(function () {
          Toast.success(I18N.t("t_trigger_on"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    document.getElementById("btn-port" + n + "-trig-off").addEventListener("click", function () {
      WS.request("set_port_trigger", {port: n, trigger: false})
        .then(function () {
          Toast.success(I18N.t("t_trigger_off"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    document.getElementById("btn-port" + n + "-impulse").addEventListener("click", function () {
      const ms = parseInt(document.getElementById("port" + n + "-impulse").value);
      if (!ms || ms < 1) {
        Toast.show(I18N.t("e_invalid_duration"), true);
        return;
      }
      WS.request("set_port_trigger", {port: n, trigger: true, duration: ms})
        .then(function () {
          Toast.success(I18N.t("t_impulse", {ms: ms}));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    document.getElementById("btn-port" + n + "-uart").addEventListener("click", function () {
      WS.request("set_port_mode", {
        port: n,
        mode: "RS232",
        uart: {
          baud_rate: parseInt(document.getElementById("port" + n + "-baud").value),
          data_bits: parseInt(document.getElementById("port" + n + "-databits").value),
          stop_bits: document.getElementById("port" + n + "-stopbits").value,
          parity: document.getElementById("port" + n + "-parity").value
        }
      })
        .then(function () {
          Toast.success(I18N.t("t_uart_applied"));
          // No re-render needed - UART settings don't change visible UI
        })
        .catch(function (e) {
          Toast.error(e);
        });
    });

    document.getElementById("btn-port" + n + "-serial-cfg").addEventListener("click", function () {
      Pages.Ports.applyBuffering(n);
    });

    document.getElementById("btn-port" + n + "-console").addEventListener("click", function () {
      const enable = !self.serialEnabled[n];
      WS.request("enable_serial_events", {port: n, enable: enable}).then(function () {
        self.serialEnabled[n] = enable;
        document.getElementById("btn-port" + n + "-console").textContent =
          enable ? I18N.t("btn_disable_console") : I18N.t("btn_enable_console");
        Toast.success(enable ? I18N.t("t_console_on") : I18N.t("t_console_off"));
      }).catch(function (e) {
        Toast.error(e);
      });
    });

    document.getElementById("btn-port" + n + "-send").addEventListener("click", function () {
      Pages.Ports.sendSerial(n);
    });
    document.getElementById("port" + n + "-input").addEventListener("keydown", function (e) {
      if (e.key === "Enter") Pages.Ports.sendSerial(n);
    });

    document.getElementById("btn-port" + n + "-clear").addEventListener("click", function () {
      document.getElementById("port" + n + "-output").textContent = "";
    });
  },

  init: function () {
    const self = this;

    this.initPort(1);
    this.initPort(2);

    // Listen for port_mode events (dock detects peripheral in AUTO mode)
    WS.on("port_mode", function (msg) {
      if (msg.port && msg.mode !== undefined) {
        self.renderPortActiveMode(msg.port, msg.mode, msg.active_mode);
      }
    });

    WS.on("serial_data", function (msg) {
      if (msg.port && msg.data) {
        self.appendConsole(msg.port, msg.data);
      }
    });
  }
};

// ============================================================
// Page: OTA
// ============================================================
Pages.OTA = {
  // Build OTA upload URL (supports DEV mode with dock parameter)
  getUploadUrl: function () {
    const urlParams = new URLSearchParams(window.location.search);
    let host = location.host;
    if (location.host === "localhost:9000" || location.host === "127.0.0.1:9000") {
      host = urlParams.get("dock") || location.host;
    }
    const proto = location.protocol === "https:" ? "https://" : "http://";
    return proto + host + "/update";
  },

  load: function () {
    // Always fetch fresh sysinfo when entering OTA page
    WS.request("get_sysinfo").then(function (data) {
      // Cache the sysinfo for reuse across pages
      UI.cacheSysInfo(data);
      document.getElementById("ota-version").textContent = data.version || "\u2014";
    }).catch(function (e) {
      // If request fails, try cached version
      const cached = UI.getCachedSysInfo();
      if (cached) {
        document.getElementById("ota-version").textContent = cached.version || "\u2014";
      } else {
        Toast.error(e);
      }
    });
  },

  resetState: function () {
    // Clear status message
    const statusEl = document.getElementById("ota-status");
    if (statusEl) {
      statusEl.textContent = "";
    }
    // Clear selected file
    const fileInput = document.getElementById("ota-file");
    if (fileInput) {
      fileInput.value = "";
      fileInput.disabled = false;
    }
    // Hide and reset progress bar
    const progress = document.getElementById("ota-progress");
    if (progress) {
      progress.classList.add("hidden");
      progress.value = 0;
    }
    // Re-enable upload button
    const uploadBtn = document.getElementById("btn-upload");
    if (uploadBtn) {
      uploadBtn.disabled = false;
    }
  },

  init: function () {
    document.getElementById("btn-upload").addEventListener("click", function () {
      const fileInput = document.getElementById("ota-file");
      if (!fileInput.files.length) {
        Toast.show(I18N.t("e_no_file"), true);
        return;
      }
      if (!WS.token) {
        Toast.show(I18N.t("e_not_auth"), true);
        return;
      }

      const file = fileInput.files[0];
      const xhr = new XMLHttpRequest();
      const progress = document.getElementById("ota-progress");
      const statusEl = document.getElementById("ota-status");

      fileInput.disabled = true;
      document.getElementById("btn-upload").disabled = true;
      progress.classList.remove("hidden");
      progress.value = 0;
      statusEl.textContent = "";

      xhr.upload.onprogress = function (e) {
        if (e.lengthComputable) {
          const pct = Math.round(e.loaded / e.total * 100);
          progress.value = pct;
          statusEl.textContent = pct + "%";
        }
      };

      xhr.onreadystatechange = function () {
        if (xhr.readyState === 4) {
          fileInput.disabled = false;
          document.getElementById("btn-upload").disabled = false;

          if (xhr.status === 200) {
            statusEl.textContent = I18N.t("t_upload_success");
            Toast.success(I18N.t("t_upload_done"));
            // Disconnect after successful update (dock will reboot)
            // Keep credentials for easy reconnection
            WS.keepConnected = false;
            // Wait 2 seconds before disconnecting to show success message
            setTimeout(function () {
              if (WS.socket && WS.socket.readyState === 1) {
                WS.socket.close();
              }
              UI.setStatus("err");
              // Automatically switch to Status page
              UI.showPage("status");
            }, 2000);
          } else if (xhr.status === 401) {
            statusEl.textContent = I18N.t("e_auth_failed");
            Toast.show(I18N.t("e_upload_auth"), true);
          } else if (xhr.status === 0) {
            statusEl.textContent = I18N.t("e_conn_lost");
            Toast.show(I18N.t("e_conn_lost"), true);
          } else {
            statusEl.textContent = I18N.t("e_upload_failed", {status: xhr.status});
            Toast.show(I18N.t("e_upload_failed", {status: xhr.status}), true);
          }
        }
      };

      xhr.onerror = function () {
        progress.classList.add("hidden");
        fileInput.disabled = false;
        document.getElementById("btn-upload").disabled = false;
        Toast.show(I18N.t("e_upload_failed", {status: "network error"}), true);
      };

      // Use full URL in DEV mode, relative path in production
      xhr.open("POST", Pages.OTA.getUploadUrl(), true);
      xhr.setRequestHeader("Authorization", "Basic " + btoa("admin:" + WS.token));
      xhr.send(file);
    });
  }
};

// ============================================================
// Page: Logs
// ============================================================
Pages.Logs = {
  streaming: false,
  filterLevel: "I",
  levelPriority: {"E": 0, "W": 1, "I": 2, "D": 3, "V": 4},

  load: function () {
    let btn = document.getElementById("btn-log-stream");
    if (btn) {
      btn.textContent = this.streaming ? I18N.t("btn_stop_streaming") : I18N.t("btn_start_streaming");
      btn.classList.toggle("btn-warn", this.streaming);
    }
    let filter = document.getElementById("log-filter");
    if (filter) {
      filter.value = this.filterLevel;
    }
  },

  formatTimestamp: function () {
    let now = new Date();
    let h = String(now.getHours()).padStart(2, "0");
    let m = String(now.getMinutes()).padStart(2, "0");
    let s = String(now.getSeconds()).padStart(2, "0");
    let ms = String(now.getMilliseconds()).padStart(3, "0");
    return h + ":" + m + ":" + s + "." + ms;
  },

  shouldShowLog: function (level) {
    let filterPriority = this.levelPriority[this.filterLevel] || 2;
    let logPriority = this.levelPriority[level] || 2;
    return logPriority <= filterPriority;
  },

  appendLog: function (msg) {
    if (!this.shouldShowLog(msg.level)) {
      return;
    }

    let output = document.getElementById("log-output");
    if (!output) return;

    let localTs = this.formatTimestamp();
    let level = msg.level;
    let line = level + " [" + localTs + " " + msg.ts + "] [" + msg.tag + "] " + msg.log + "\n";

    output.textContent += line;

    let container = output.parentElement;
    container.scrollTop = container.scrollHeight;

    if (output.textContent.length > 32768) {
      output.textContent = output.textContent.slice(-30720);
    }
  },

  toggleStreaming: function () {
    const self = this;
    let enable = !this.streaming;

    WS.request("enable_log_events", {enable: enable})
      .then(function () {
        self.streaming = enable;
        let btn = document.getElementById("btn-log-stream");
        if (btn) {
          btn.textContent = enable ? I18N.t("btn_stop_streaming") : I18N.t("btn_start_streaming");
          btn.classList.toggle("btn-warn", enable);
        }
      })
      .catch(function (e) {
        Toast.error(e);
      });
  },

  clearLog: function () {
    let output = document.getElementById("log-output");
    if (output) {
      output.textContent = "";
    }
  },

  init: function () {
    const self = this;

    document.getElementById("btn-log-stream").addEventListener("click", function () {
      self.toggleStreaming();
    });

    document.getElementById("btn-log-clear").addEventListener("click", function () {
      self.clearLog();
    });

    document.getElementById("log-filter").addEventListener("change", function () {
      self.filterLevel = this.value;
    });

    WS.on("log", function (msg) {
      self.appendLog(msg);
    });
  }
};


// ============================================================
// Page: Expert
// ============================================================
Pages.Expert = {
  config: {
    itach_emulation: false,
    itach_beacon: false,
    serial_tcp: false,
    poe_voltage: false,
    ntp_enabled: false,
    ntp_server1: "",
    ntp_server2: ""
  },
  deviceRevision: null,

  load: function () {
    const self = this;

    // Get revision from cached sysinfo (no separate request needed)
    const cached = UI.getCachedSysInfo();
    if (cached) {
      self.deviceRevision = cached.revision || null;
      self.updatePoeVisibility();

      // Get PoE mode if revision 6
      if (self.deviceRevision === "6") {
        self.config.poe_voltage = cached.poe_mode === 1;
        document.getElementById("exp-poe-voltage").checked = self.config.poe_voltage;
      }
    }

    // Get IR config (includes iTach settings)
    WS.request("get_ir_config").then(function (data) {
      if (data.itach_emulation !== undefined) {
        self.config.itach_emulation = data.itach_emulation;
        document.getElementById("exp-itach-emulation").checked = data.itach_emulation;
      }
      if (data.itach_beacon !== undefined) {
        self.config.itach_beacon = data.itach_beacon;
        document.getElementById("exp-amxb-beacon").checked = data.itach_beacon;
      }
    }).catch(function (e) {
      Toast.error(e);
    });

    // Get serial TCP config
    WS.request("get_serial_tcp").then(function (data) {
      if (data.serial_tcp !== undefined) {
        self.config.serial_tcp = data.serial_tcp;
        document.getElementById("exp-rs232-tcp").checked = data.serial_tcp;
      }
    }).catch(function (e) {
      Toast.error(e);
    });

    // Get network configuration
    WS.request("get_network").then(function (data) {
      // NTP configuration
      if (data.ntp_enabled === undefined) {
        data.ntp_enabled = false;
      }
      self.config.ntp_enabled = data.ntp_enabled;
      document.getElementById("exp-ntp-enabled").checked = data.ntp_enabled;
      if (data.ntp1 !== undefined) {
        self.config.ntp_server1 = data.ntp1;
        document.getElementById("exp-ntp1").value = data.ntp1;
      }
      if (data.ntp2 !== undefined) {
        self.config.ntp_server2 = data.ntp2;
        document.getElementById("exp-ntp2").value = data.ntp2;
      }
      self.updateNtpFields(self.config.ntp_enabled);

    }).catch(function (e) {
      Toast.error(e);
    });
  },

  updatePoeVisibility: function () {
    const poeRow = document.getElementById("exp-poe-row");
    if (poeRow) {
      poeRow.classList.toggle("hidden", this.deviceRevision !== "6");
    }
  },

  updateNtpFields: function (enabled) {
    document.getElementById("exp-ntp1").disabled = !enabled;
    document.getElementById("exp-ntp2").disabled = !enabled;
  },

  apply: function () {
    const self = this;
    const itachEmulation = document.getElementById("exp-itach-emulation").checked;
    const itachBeacon = document.getElementById("exp-amxb-beacon").checked;
    const serialTcp = document.getElementById("exp-rs232-tcp").checked;
    const poeVoltage = document.getElementById("exp-poe-voltage").checked;
    const ntpEnabled = document.getElementById("exp-ntp-enabled").checked;
    const ntpServer1 = document.getElementById("exp-ntp1").value.trim();
    const ntpServer2 = document.getElementById("exp-ntp2").value.trim();

    // Apply iTach config if changed
    if (itachEmulation !== this.config.itach_emulation || itachBeacon !== this.config.itach_beacon) {
      WS.request("set_ir_config", {
        itach_emulation: itachEmulation,
        itach_beacon: itachBeacon
      }).then(function (data) {
        self.config.itach_emulation = itachEmulation;
        self.config.itach_beacon = itachBeacon;
        Toast.success(I18N.t("t_expert_saved"));
      }).catch(function (e) {
        Toast.error(e);
      });
    }

    // Apply serial TCP config if changed
    if (serialTcp !== this.config.serial_tcp) {
      WS.request("set_serial_tcp", {enable: serialTcp})
        .then(function () {
          self.config.serial_tcp = serialTcp;
          Toast.success(I18N.t("t_expert_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    }

    // Apply PoE voltage config if changed (rev6 only)
    if (this.deviceRevision === "6" && poeVoltage !== this.config.poe_voltage) {
      WS.request("set_poe", {mode: poeVoltage ? 1 : 0})
        .then(function () {
          self.config.poe_voltage = poeVoltage;
          Toast.success(I18N.t("t_expert_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    }

    // Apply NTP config if changed
    if (ntpEnabled !== this.config.ntp_enabled ||
      ntpServer1 !== this.config.ntp_server1 ||
      ntpServer2 !== this.config.ntp_server2) {
      WS.request("set_ntp", {
        ntp_enabled: ntpEnabled,
        ntp1: ntpServer1,
        ntp2: ntpServer2
      })
        .then(function () {
          self.config.ntp_enabled = ntpEnabled;
          self.config.ntp_server1 = ntpServer1;
          self.config.ntp_server2 = ntpServer2;
          Toast.success(I18N.t("t_ntp_saved"));
        })
        .catch(function (e) {
          Toast.error(e);
        });
    }
  },

  init: function () {
    const self = this;

    // NTP enabled checkbox change
    document.getElementById("exp-ntp-enabled").addEventListener("change", function () {
      self.updateNtpFields(this.checked);
    });

    document.getElementById("btn-expert-apply").addEventListener("click", function () {
      self.apply();
    });
  }
};

// ============================================================
// Application Initialization
// ============================================================
document.addEventListener("DOMContentLoaded", function () {
  Theme.init();
  I18N.init();
  Pages.General.init();
  Pages.Network.init();
  Pages.IR.init();
  Pages.Ports.init();
  Pages.Logs.init();
  Pages.OTA.init();
  Pages.Expert.init();
  UI.init();

  // Dev mode indicator (localhost testing)
  if (location.hostname === "localhost" || location.hostname === "127.0.0.1") {
    console.log("DEV MODE: Connected to", WS.url);
    document.body.classList.add("dev-mode");
  }
});
