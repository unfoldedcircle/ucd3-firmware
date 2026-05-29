"use strict";

// ============================================================
// Theme Management
// ============================================================
var Theme = {
  _theme: "dark",

  init: function() {
    this._theme = localStorage.getItem("theme") || "dark";
    this.apply(this._theme);
    this.initSelector();
  },

  apply: function(theme) {
    this._theme = theme;
    document.documentElement.setAttribute("data-theme", theme);
    localStorage.setItem("theme", theme);
  },

  initSelector: function() {
    var select = document.getElementById("theme-select");
    if (!select) return;

    select.value = this._theme;

    var self = this;
    select.addEventListener("change", function() {
      self.apply(this.value);
    });
  }
};

// ============================================================
// Internationalization
// ============================================================
var I18N = {
  _lang: "en",
  _strings: {},
  _available: [],

  init: function() {
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

  detect: function() {
    var stored = localStorage.getItem("lang");
    if (stored && LANG_DATA[stored]) return stored;

    var nav = (navigator.language || "en").split("-")[0].toLowerCase();
    if (LANG_DATA[nav]) return nav;

    return "en";
  },

  resolve: function(lang) {
    var base = LANG_DATA.en || {};
    var overlay = LANG_DATA[lang] || {};
    var result = {};
    for (var k in base) {
      if (base.hasOwnProperty(k)) {
        result[k] = overlay[k] || base[k];
      }
    }
    return result;
  },

  setLang: function(lang) {
    if (!LANG_DATA[lang]) return;
    this._lang = lang;
    localStorage.setItem("lang", lang);
    this._strings = this.resolve(lang);
    this.applyDom();
    var select = document.getElementById("lang-select");
    if (select) select.value = lang;
    // Re-render dynamic content on current page
    UI.loadPage(UI.currentPage);
    UI.updateLogoutVisibility();
    UI.setStatus(WS.authenticated ? "ok" : (WS.socket && WS.socket.readyState === 0 ? "connecting" : "err"));
  },

  applyDom: function() {
    var strings = this._strings;

    document.querySelectorAll("[data-i18n]").forEach(function(el) {
      var key = el.getAttribute("data-i18n");
      if (strings[key]) el.textContent = strings[key];
    });

    document.querySelectorAll("[data-i18n-html]").forEach(function(el) {
      var key = el.getAttribute("data-i18n-html");
      if (strings[key]) el.innerHTML = strings[key];
    });

    document.querySelectorAll("[data-i18n-ph]").forEach(function(el) {
      var key = el.getAttribute("data-i18n-ph");
      if (strings[key]) el.placeholder = strings[key];
    });

    document.querySelectorAll("[data-i18n-title]").forEach(function(el) {
      var key = el.getAttribute("data-i18n-title");
      if (strings[key]) el.title = strings[key];
    });
  },

  initSelector: function() {
    var select = document.getElementById("lang-select");
    if (!select) return;

    select.innerHTML = "";
    var self = this;
    this._available.sort().forEach(function(code) {
      var opt = document.createElement("option");
      opt.value = code;
      opt.textContent = code.toUpperCase();
      select.appendChild(opt);
    });
    select.value = this._lang;

    select.addEventListener("change", function() {
      self.setLang(this.value);
    });
  },

  t: function(key, replacements) {
    var str = this._strings[key] || key;
    if (replacements) {
      for (var k in replacements) {
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
var WS = {
  socket: null,
  url: (function() {
    var urlParams = new URLSearchParams(window.location.search);
    var host = location.host;
    if (location.host === "localhost:9000" || location.host === "127.0.0.1:9000") {
      host = urlParams.get("host") || "192.168.1.100";
    }
    return "ws://" + host + "/ws";
  })(),
  token: null,
  authenticated: false,
  msgId: 0,
  pending: {},
  reconnectMs: 2000,
  maxReconnectMs: 30000,
  reconnectTimer: null,
  eventHandlers: {},

  connect: function() {
    if (this.socket && this.socket.readyState < 2) return;
    clearTimeout(this.reconnectTimer);
    this.socket = new WebSocket(this.url);
    this.socket.onopen = function() { UI.setStatus("connecting"); };
    var self = this;
    this.socket.onclose = function() {
      self.authenticated = false;
      self.rejectAllPending();
      self.scheduleReconnect();
    };
    this.socket.onerror = function() {};
    this.socket.onmessage = function(e) { self.onMessage(JSON.parse(e.data)); };
  },

  onMessage: function(msg) {
    if (msg.type === "auth_required") {
      if (this.token) {
        this.send({ type: "auth", token: this.token });
        UI.setStatus("connecting");
      } else {
        UI.setStatus("err");
        if (UI.currentPage !== "status") {
          UI.showLogin();
        }
      }
      // Load status page regardless (no auth needed)
      if (UI.currentPage === "status") {
        Pages.Status.load();
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
      } else {
        this.token = null;
        sessionStorage.removeItem("token");
        UI.setStatus("err");
        UI.showLogin(I18N.t("e_invalid_token"));
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
      var p = this.pending[msg.req_id];
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
      var h = this.eventHandlers[msg.msg];
      if (h) h(msg);
    }
  },

  request: function(command, data) {
    var id = ++this.msgId;
    var msg = { type: "dock", id: id, command: command };
    if (data) {
      for (var k in data) {
        if (data.hasOwnProperty(k)) msg[k] = data[k];
      }
    }
    var self = this;
    return new Promise(function(resolve, reject) {
      var timer = setTimeout(function() {
        delete self.pending[id];
        reject({ code: 408, msg: "timeout" });
      }, 10000);
      self.pending[id] = { resolve: resolve, reject: reject, timer: timer };
      try {
        self.socket.send(JSON.stringify(msg));
      } catch (e) {
        clearTimeout(timer);
        delete self.pending[id];
        reject({ code: 503, msg: "send failed" });
      }
    });
  },

  send: function(msg) {
    try {
      this.socket.send(JSON.stringify(msg));
    } catch (e) {
      // connection lost, onclose will handle reconnect
    }
  },

  scheduleReconnect: function() {
    UI.setStatus("err");
    var self = this;
    this.reconnectTimer = setTimeout(function() { self.connect(); }, this.reconnectMs);
    this.reconnectMs = Math.min(Math.round(this.reconnectMs * 1.5), this.maxReconnectMs);
  },

  rejectAllPending: function() {
    for (var id in this.pending) {
      if (this.pending.hasOwnProperty(id)) {
        clearTimeout(this.pending[id].timer);
        this.pending[id].reject({ code: 503, msg: "disconnected" });
      }
    }
    this.pending = {};
  },

  on: function(msg, handler) { this.eventHandlers[msg] = handler; },
  off: function(msg) { delete this.eventHandlers[msg]; },

  authenticate: function(token) {
    this.token = token;
    if (this.socket && this.socket.readyState === 1) {
      this.send({ type: "auth", token: token });
      UI.setStatus("connecting");
    } else {
      this.connect();
    }
  }
};

// ============================================================
// Toast Notification
// ============================================================
var Toast = {
  timer: null,

  show: function(message, isError) {
    var el = document.getElementById("toast");
    el.textContent = message;
    el.className = "toast " + (isError ? "toast-err" : "toast-ok");
    el.classList.remove("hidden");
    clearTimeout(this.timer);
    this.timer = setTimeout(function() {
      el.classList.add("hidden");
    }, isError ? 5000 : 3000);
  },

  error: function(msg) {
    var text = "Error";
    if (msg && msg.code) text += " " + msg.code;
    if (msg && msg.msg) text += ": " + msg.msg;
    this.show(text, true);
  },

  success: function(message) {
    this.show(message || "OK", false);
  }
};

// ============================================================
// UI Controller
// ============================================================
var UI = {
  currentPage: "status",
  pendingPage: null,
  authRequired: { general: true, network: true, ir: true, ports: true, ota: true, logs: true, expert: true },

  init: function() {
    var self = this;

    // Nav click handlers
    document.querySelectorAll("#main-nav a").forEach(function(a) {
      a.addEventListener("click", function(e) {
        e.preventDefault();
        self.showPage(a.dataset.page);
      });
    });

    // Login handlers
    document.getElementById("login-btn").addEventListener("click", function() {
      var pw = document.getElementById("login-pw").value;
      if (pw.length >= 4) {
        WS.authenticate(pw);
      }
    });
    document.getElementById("login-pw").addEventListener("keydown", function(e) {
      if (e.key === "Enter") document.getElementById("login-btn").click();
    });

    // ESC closes login overlay
    document.addEventListener("keydown", function(e) {
      if (e.key === "Escape" && !document.getElementById("login-overlay").classList.contains("hidden")) {
        self.hideLogin();
        self.pendingPage = null;
      }
    });

    // Logout
    document.getElementById("btn-logout").addEventListener("click", function() {
      WS.token = null;
      WS.authenticated = false;
      sessionStorage.removeItem("token");
      if (WS.socket && WS.socket.readyState === 1) {
        WS.socket.close();
      }
      self.showPage("status");
      Toast.success(I18N.t("t_logged_out"));
    });

    // Restore token from session
    WS.token = sessionStorage.getItem("token");

    // Connect WebSocket
    WS.connect();
  },

  showPage: function(page) {
    if (this.authRequired[page] && !WS.authenticated) {
      this.pendingPage = page;
      this.showLogin();
      return;
    }
    document.querySelectorAll("#main-nav a").forEach(function(a) {
      a.classList.toggle("active", a.dataset.page === page);
    });
    document.querySelectorAll("main > section").forEach(function(s) {
      s.classList.toggle("hidden", s.id !== "page-" + page);
    });
    this.currentPage = page;
    this.loadPage(page);
    this.updateLogoutVisibility();
  },

  loadPage: function(page) {
    switch (page) {
      case "status": Pages.Status.load(); break;
      case "general": Pages.General.load(); break;
      case "network": Pages.Network.load(); break;
      case "ir": Pages.IR.load(); break;
      case "ports": Pages.Ports.load(); break;
      case "logs": Pages.Logs.load(); break;
      case "ota": Pages.OTA.load(); break;
      case "expert": Pages.Expert.load(); break;
    }
  },

  onAuthenticated: function() {
    var page = this.pendingPage || this.currentPage;
    this.pendingPage = null;
    this.showPage(page);
    this.updateLogoutVisibility();
  },

  updateLogoutVisibility: function() {
    var btn = document.getElementById("btn-logout");
    if (btn) btn.classList.toggle("hidden", !WS.authenticated);
  },

  setStatus: function(state) {
    var dot = document.querySelector("#conn-status .status-dot");
    var text = document.getElementById("conn-text");
    if (state === "connecting") {
      dot.className = "status-dot warn";
    } else {
      dot.className = "status-dot " + state;
    }
    text.textContent = I18N.t("st_" + (state === "connecting" ? "connecting" : (state === "ok" ? "connected" : "disconnected")));
    this.updateLogoutVisibility();
  },

  showLogin: function(errMsg) {
    var overlay = document.getElementById("login-overlay");
    overlay.classList.remove("hidden");
    var errEl = document.getElementById("login-err");
    if (errMsg) {
      errEl.textContent = errMsg;
      errEl.classList.remove("hidden");
    } else {
      errEl.classList.add("hidden");
    }
    document.getElementById("login-pw").value = "";
    document.getElementById("login-pw").focus();
  },

  hideLogin: function() {
    document.getElementById("login-overlay").classList.add("hidden");
  }
};

// ============================================================
// Page: Status
// ============================================================
var Pages = {};

Pages.Status = {
  load: function() {
    WS.request("get_sysinfo").then(function(data) {
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
      document.getElementById("s-heap").textContent = data.free_heap || "\u2014";
      document.getElementById("s-reset").textContent = data.reset_reason || "\u2014";
    }).catch(function() {});
  }
};

// ============================================================
// Page: General
// ============================================================
Pages.General = {
  load: function() {
    WS.request("get_sysinfo").then(function(data) {
      document.getElementById("cfg-name").value = data.name || "";
      var ledPct = Math.round((data.led_brightness || 0) / 255 * 100);
      var ethPct = Math.round((data.eth_led_brightness || 0) / 255 * 100);
      document.getElementById("cfg-led").value = ledPct;
      document.getElementById("cfg-led-val").textContent = ledPct + "%";
      document.getElementById("cfg-eth-led").value = ethPct;
      document.getElementById("cfg-eth-led-val").textContent = ethPct + "%";
    }).catch(function(e) { Toast.error(e); });
  },

  init: function() {
    document.getElementById("cfg-led").addEventListener("input", function() {
      document.getElementById("cfg-led-val").textContent = this.value + "%";
    });
    document.getElementById("cfg-eth-led").addEventListener("input", function() {
      document.getElementById("cfg-eth-led-val").textContent = this.value + "%";
    });

    document.getElementById("btn-save-name").addEventListener("click", function() {
      var name = document.getElementById("cfg-name").value.trim();
      if (!name) return;
      WS.request("set_config", { friendly_name: name })
          .then(function() { Toast.success(I18N.t("t_name_saved")); })
          .catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-save-brightness").addEventListener("click", function() {
      var ledPct = parseInt(document.getElementById("cfg-led").value);
      var ethPct = parseInt(document.getElementById("cfg-eth-led").value);
      WS.request("set_brightness", {
        status_led: Math.round(ledPct / 100 * 255),
        eth_led: Math.round(ethPct / 100 * 255)
      })
          .then(function() { Toast.success(I18N.t("t_brightness_saved")); })
          .catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-save-token").addEventListener("click", function() {
      var t = document.getElementById("cfg-token").value;
      if (t.length < 4) {
        Toast.show(I18N.t("e_token_min"), true);
        return;
      }
      WS.request("set_config", { token: t }).then(function() {
        WS.token = t;
        sessionStorage.setItem("token", t);
        document.getElementById("cfg-token").value = "";
        Toast.success(I18N.t("t_token_changed"));
      }).catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-identify").addEventListener("click", function() {
      WS.request("identify")
          .then(function() { Toast.success(I18N.t("t_identify")); })
          .catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-reboot").addEventListener("click", function() {
      if (confirm(I18N.t("confirm_reboot"))) {
        WS.request("reboot").catch(function() {});
        Toast.success(I18N.t("t_rebooting"));
      }
    });

    document.getElementById("btn-reset").addEventListener("click", function() {
      if (confirm(I18N.t("confirm_reset"))) {
        WS.request("reset").catch(function() {});
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
  load: function() {
    WS.request("get_sysinfo").then(function(data) {
      document.getElementById("net-eth").textContent = data.ethernet
          ? I18N.t("st_connected") : I18N.t("st_disconnected");
      document.getElementById("net-wifi").textContent = data.wifi
          ? I18N.t("st_connected") : I18N.t("st_disconnected");
      document.getElementById("net-ssid").textContent = data.ssid || "\u2014";
      document.getElementById("cfg-ssid").value = data.ssid || "";
    }).catch(function(e) { Toast.error(e); });
  },

  init: function() {
    // WiFi save
    document.getElementById("btn-save-wifi").addEventListener("click", function() {
      var ssid = document.getElementById("cfg-ssid").value.trim();
      var pw = document.getElementById("cfg-wifi-pw").value;
      if (!ssid) {
        Toast.show(I18N.t("e_ssid_required"), true);
        return;
      }
      if (!pw) {
        Toast.show(I18N.t("e_pw_required"), true);
        return;
      }
      if (!confirm(I18N.t("confirm_wifi"))) return;
      WS.request("set_config", { ssid: ssid, wifi_password: pw })
          .then(function() { Toast.success(I18N.t("t_wifi_saved")); })
          .catch(function(e) { Toast.error(e); });
    });

    // Password reveal toggle
    document.getElementById("btn-reveal-wifi").addEventListener("click", function() {
      var input = document.getElementById("cfg-wifi-pw");
      var show = input.type === "password";
      input.type = show ? "text" : "password";
      this.textContent = show ? I18N.t("btn_hide") : I18N.t("btn_show");
    });
  }
};

// ============================================================
// Page: IR
// ============================================================
Pages.IR = {
  learning: false,
  portModes: { 1: null, 2: null },

  load: function() {
    var self = this;
    // Get port modes to determine checkbox availability
    WS.request("get_port_modes").then(function(data) {
      if (data.ports) {
        data.ports.forEach(function(p) {
          self.portModes[p.port] = p.active_mode || p.mode;
        });
      }
      self.updateOutputCheckboxes();
    }).catch(function() {});

    // Get sysinfo for ir_learning state
    WS.request("get_sysinfo").then(function(data) {
      self.learning = data.ir_learning || false;
      self.updateLearnButton();
    }).catch(function() {});
  },

  isIrOutput: function(mode) {
    return mode === "IR_BLASTER" || mode === "IR_EMITTER_MONO_PLUG" || mode === "IR_EMITTER_STEREO_PLUG";
  },

  updateOutputCheckboxes: function() {
    var ext1 = document.getElementById("ir-ext1");
    var ext2 = document.getElementById("ir-ext2");
    var port1IsIr = this.isIrOutput(this.portModes[1]);
    var port2IsIr = this.isIrOutput(this.portModes[2]);

    ext1.disabled = !port1IsIr;
    ext2.disabled = !port2IsIr;
    if (!port1IsIr) ext1.checked = false;
    if (!port2IsIr) ext2.checked = false;

    document.getElementById("ir-ext1-label").classList.toggle("disabled", !port1IsIr);
    document.getElementById("ir-ext2-label").classList.toggle("disabled", !port2IsIr);
  },

  updateLearnButton: function() {
    var btn = document.getElementById("btn-ir-learn");
    btn.textContent = this.learning ? I18N.t("btn_stop_learning") : I18N.t("btn_start_learning");
    btn.classList.toggle("btn-warn", this.learning);
  },

  init: function() {
    var self = this;

    // Send IR
    document.getElementById("btn-ir-send").addEventListener("click", function() {
      var code = document.getElementById("ir-code").value.trim();
      if (!code) {
        Toast.show(I18N.t("e_ir_required"), true);
        return;
      }

      var format = document.getElementById("ir-format").value;
      var data = {
        code: code,
        format: format,
        int_side: document.getElementById("ir-int-side").checked,
        int_top: false,
        ext1: document.getElementById("ir-ext1").checked,
        ext2: document.getElementById("ir-ext2").checked
      };

      WS.request("ir_send", data)
          .then(function() { Toast.success(I18N.t("t_ir_sent")); })
          .catch(function(e) { Toast.error(e); });
    });

    // Learn IR toggle
    document.getElementById("btn-ir-learn").addEventListener("click", function() {
      var command = self.learning ? "ir_receive_off" : "ir_receive_on";
      WS.request(command).then(function() {
        self.learning = !self.learning;
        self.updateLearnButton();
        Toast.success(self.learning ? I18N.t("t_ir_learn_on") : I18N.t("t_ir_learn_off"));
      }).catch(function(e) { Toast.error(e); });
    });

    // Clear learned codes
    document.getElementById("btn-ir-clear").addEventListener("click", function() {
      document.getElementById("ir-learn-output").textContent = "";
    });

    // Listen for ir_receive events
    WS.on("ir_receive", function(msg) {
      var output = document.getElementById("ir-learn-output");
      if (!output) return;
      if (output.textContent) output.textContent += "\n";
      output.textContent += msg.ir_code;
      var container = output.parentElement;
      container.scrollTop = container.scrollHeight;
    });
  }
};

// ============================================================
// Page: Ports
// ============================================================
Pages.Ports = {
  serialEnabled: { 1: false, 2: false },

  load: function() {
    var self = this;
    WS.request("get_port_modes").then(function(data) {
      if (data.ports) {
        data.ports.forEach(function(p) { self.renderPort(p); });
      }
    }).catch(function(e) { Toast.error(e); });
  },

  renderPort: function(portData) {
    var n = portData.port;
    var modeSelect = document.getElementById("port" + n + "-mode");
    if (modeSelect) modeSelect.value = portData.mode;

    var activeEl = document.getElementById("port" + n + "-active");
    if (activeEl) activeEl.textContent = portData.active_mode || "\u2014";

    var activeMode = portData.active_mode || portData.mode;
    var isTrigger = activeMode === "TRIGGER_5V";
    var isRS232 = activeMode === "RS232";

    var triggerEl = document.getElementById("port" + n + "-trigger");
    var rs232El = document.getElementById("port" + n + "-rs232");
    if (triggerEl) triggerEl.classList.toggle("hidden", !isTrigger);
    if (rs232El) rs232El.classList.toggle("hidden", !isRS232);

    if (isRS232) {
      this.loadSerialConfig(n);
    }
  },

  loadSerialConfig: function(port) {
    WS.request("get_serial_config", { port: port }).then(function(cfg) {
      var p = port;
      if (cfg.baud_rate !== undefined) document.getElementById("port" + p + "-baud").value = cfg.baud_rate;
      if (cfg.data_bits !== undefined) document.getElementById("port" + p + "-databits").value = cfg.data_bits;
      if (cfg.stop_bits !== undefined) document.getElementById("port" + p + "-stopbits").value = cfg.stop_bits;
      if (cfg.parity) document.getElementById("port" + p + "-parity").value = cfg.parity;
      if (cfg.buffering) document.getElementById("port" + p + "-buffering").value = cfg.buffering;
      if (cfg.terminator !== undefined) {
        document.getElementById("port" + p + "-terminator").value = Pages.Ports.charToHex(cfg.terminator);
      }
    }).catch(function() {});
  },

  charToHex: function(str) {
    if (!str || str.length === 0) return "0x0A";
    return "0x" + str.charCodeAt(0).toString(16).toUpperCase().padStart(2, "0");
  },

  hexToChar: function(hex) {
    var s = hex.trim().replace(/^0x/i, "");
    var code = parseInt(s, 16);
    if (isNaN(code) || code < 0 || code > 0xFF) return "\n";
    return String.fromCharCode(code);
  },

  getTerminatorChar: function(port) {
    var hex = document.getElementById("port" + port + "-terminator").value;
    return this.hexToChar(hex);
  },

  sendSerial: function(port) {
    var input = document.getElementById("port" + port + "-input");
    var data = input.value;
    if (!data) return;

    // Append terminator if buffering mode is "line"
    var buffering = document.getElementById("port" + port + "-buffering").value;
    if (buffering === "line") {
      data += this.getTerminatorChar(port);
    }

    WS.request("send_serial", { port: port, data: data })
        .catch(function(e) { Toast.error(e); });
    input.value = "";
  },

  appendConsole: function(port, data) {
    var output = document.getElementById("port" + port + "-output");
    if (!output) return;
    output.textContent += data;
    var container = output.parentElement;
    container.scrollTop = container.scrollHeight;
    if (output.textContent.length > 4096) {
      output.textContent = output.textContent.slice(-3072);
    }
  },

  initPort: function(n) {
    var self = this;

    document.getElementById("btn-port" + n + "-mode").addEventListener("click", function() {
      var mode = document.getElementById("port" + n + "-mode").value;
      var data = { port: n, mode: mode };

      if (mode === "RS232") {
        data.uart = {
          baud_rate: parseInt(document.getElementById("port" + n + "-baud").value),
          data_bits: parseInt(document.getElementById("port" + n + "-databits").value),
          stop_bits: document.getElementById("port" + n + "-stopbits").value,
          parity: document.getElementById("port" + n + "-parity").value
        };
      }

      WS.request("set_port_mode", data).then(function() {
        Toast.success(I18N.t("t_port_mode", { port: n, mode: mode }));
        self.load();
      }).catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-port" + n + "-trig-on").addEventListener("click", function() {
      WS.request("set_port_trigger", { port: n, trigger: true })
          .then(function() { Toast.success(I18N.t("t_trigger_on")); })
          .catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-port" + n + "-trig-off").addEventListener("click", function() {
      WS.request("set_port_trigger", { port: n, trigger: false })
          .then(function() { Toast.success(I18N.t("t_trigger_off")); })
          .catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-port" + n + "-impulse").addEventListener("click", function() {
      var ms = parseInt(document.getElementById("port" + n + "-impulse").value);
      if (!ms || ms < 1) {
        Toast.show(I18N.t("e_invalid_duration"), true);
        return;
      }
      WS.request("set_port_trigger", { port: n, trigger: true, duration: ms })
          .then(function() { Toast.success(I18N.t("t_impulse", { ms: ms })); })
          .catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-port" + n + "-uart").addEventListener("click", function() {
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
          .then(function() { Toast.success(I18N.t("t_uart_applied")); })
          .catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-port" + n + "-serial-cfg").addEventListener("click", function() {
      var hex = document.getElementById("port" + n + "-terminator").value;
      WS.request("set_serial_config", {
        port: n,
        buffering: document.getElementById("port" + n + "-buffering").value,
        terminator: Pages.Ports.hexToChar(hex)
      })
          .then(function() { Toast.success(I18N.t("t_buffering_applied")); })
          .catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-port" + n + "-console").addEventListener("click", function() {
      var enable = !self.serialEnabled[n];
      WS.request("enable_serial_events", { port: n, enable: enable }).then(function() {
        self.serialEnabled[n] = enable;
        document.getElementById("btn-port" + n + "-console").textContent =
            enable ? I18N.t("btn_disable_console") : I18N.t("btn_enable_console");
        Toast.success(enable ? I18N.t("t_console_on") : I18N.t("t_console_off"));
      }).catch(function(e) { Toast.error(e); });
    });

    document.getElementById("btn-port" + n + "-send").addEventListener("click", function() {
      Pages.Ports.sendSerial(n);
    });
    document.getElementById("port" + n + "-input").addEventListener("keydown", function(e) {
      if (e.key === "Enter") Pages.Ports.sendSerial(n);
    });

    document.getElementById("btn-port" + n + "-clear").addEventListener("click", function() {
      document.getElementById("port" + n + "-output").textContent = "";
    });
  },

  init: function() {
    this.initPort(1);
    this.initPort(2);

    var self = this;
    WS.on("serial_data", function(msg) {
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
  load: function() {
    WS.request("get_sysinfo").then(function(data) {
      document.getElementById("ota-version").textContent = data.version || "\u2014";
    }).catch(function() {});
  },

  init: function() {
    document.getElementById("btn-upload").addEventListener("click", function() {
      var fileInput = document.getElementById("ota-file");
      if (!fileInput.files.length) {
        Toast.show(I18N.t("e_no_file"), true);
        return;
      }
      if (!WS.token) {
        Toast.show(I18N.t("e_not_auth"), true);
        return;
      }

      var file = fileInput.files[0];
      var xhr = new XMLHttpRequest();
      var progress = document.getElementById("ota-progress");
      var statusEl = document.getElementById("ota-status");

      fileInput.disabled = true;
      document.getElementById("btn-upload").disabled = true;
      progress.classList.remove("hidden");
      progress.value = 0;
      statusEl.textContent = "";

      xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
          var pct = Math.round(e.loaded / e.total * 100);
          progress.value = pct;
          statusEl.textContent = pct + "%";
        }
      };

      xhr.onreadystatechange = function() {
        if (xhr.readyState === 4) {
          fileInput.disabled = false;
          document.getElementById("btn-upload").disabled = false;

          if (xhr.status === 200) {
            statusEl.textContent = I18N.t("t_upload_success");
            Toast.success(I18N.t("t_upload_done"));
          } else if (xhr.status === 401) {
            statusEl.textContent = I18N.t("e_auth_failed");
            Toast.show(I18N.t("e_upload_auth"), true);
          } else if (xhr.status === 0) {
            statusEl.textContent = I18N.t("e_conn_lost");
            Toast.show(I18N.t("e_conn_lost"), true);
          } else {
            statusEl.textContent = I18N.t("e_upload_failed", { status: xhr.status });
            Toast.show(I18N.t("e_upload_failed", { status: xhr.status }), true);
          }
        }
      };

      xhr.open("POST", "/update", true);
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
  levelPriority: { "E": 0, "W": 1, "I": 2, "D": 3, "V": 4 },

  load: function() {
    var btn = document.getElementById("btn-log-stream");
    if (btn) {
      btn.textContent = this.streaming ? I18N.t("btn_stop_streaming") : I18N.t("btn_start_streaming");
      btn.classList.toggle("btn-warn", this.streaming);
    }
    var filter = document.getElementById("log-filter");
    if (filter) {
      filter.value = this.filterLevel;
    }
  },

  formatTimestamp: function() {
    var now = new Date();
    var h = String(now.getHours()).padStart(2, "0");
    var m = String(now.getMinutes()).padStart(2, "0");
    var s = String(now.getSeconds()).padStart(2, "0");
    var ms = String(now.getMilliseconds()).padStart(3, "0");
    return h + ":" + m + ":" + s + "." + ms;
  },

  shouldShowLog: function(level) {
    var filterPriority = this.levelPriority[this.filterLevel] || 2;
    var logPriority = this.levelPriority[level] || 2;
    return logPriority <= filterPriority;
  },

  appendLog: function(msg) {
    if (!this.shouldShowLog(msg.level)) {
      return;
    }

    var output = document.getElementById("log-output");
    if (!output) return;

    var localTs = this.formatTimestamp();
    var level = msg.level;
    var line = level + " [" + localTs + " " + msg.ts + "] [" + msg.tag + "] " + msg.log + "\n";

    output.textContent += line;

    var container = output.parentElement;
    container.scrollTop = container.scrollHeight;

    if (output.textContent.length > 32768) {
      output.textContent = output.textContent.slice(-30720);
    }
  },

  toggleStreaming: function() {
    var self = this;
    var enable = !this.streaming;

    WS.request("enable_log_events", { enable: enable })
        .then(function() {
          self.streaming = enable;
          var btn = document.getElementById("btn-log-stream");
          if (btn) {
            btn.textContent = enable ? I18N.t("btn_stop_streaming") : I18N.t("btn_start_streaming");
            btn.classList.toggle("btn-warn", enable);
          }
        })
        .catch(function(e) {
          Toast.error(e);
        });
  },

  clearLog: function() {
    var output = document.getElementById("log-output");
    if (output) {
      output.textContent = "";
    }
  },

  init: function() {
    var self = this;

    document.getElementById("btn-log-stream").addEventListener("click", function() {
      self.toggleStreaming();
    });

    document.getElementById("btn-log-clear").addEventListener("click", function() {
      self.clearLog();
    });

    document.getElementById("log-filter").addEventListener("change", function() {
      self.filterLevel = this.value;
    });

    WS.on("log", function(msg) {
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
    serial_tcp: false
  },

  load: function() {
    var self = this;

    // Get IR config (includes iTach settings)
    WS.request("get_ir_config").then(function(data) {
      if (data.itach_emulation !== undefined) {
        self.config.itach_emulation = data.itach_emulation;
        document.getElementById("exp-itach-emulation").checked = data.itach_emulation;
      }
      if (data.itach_beacon !== undefined) {
        self.config.itach_beacon = data.itach_beacon;
        document.getElementById("exp-amxb-beacon").checked = data.itach_beacon;
      }
    }).catch(function() {});

    // Get serial TCP config
    WS.request("get_serial_tcp").then(function(data) {
      if (data.serial_tcp !== undefined) {
        self.config.serial_tcp = data.serial_tcp;
        document.getElementById("exp-rs232-tcp").checked = data.serial_tcp;
      }
    }).catch(function() {});
  },

  apply: function() {
    var self = this;
    var itachEmulation = document.getElementById("exp-itach-emulation").checked;
    var itachBeacon = document.getElementById("exp-amxb-beacon").checked;
    var serialTcp = document.getElementById("exp-rs232-tcp").checked;

    // Apply iTach config if changed
    if (itachEmulation !== this.config.itach_emulation || itachBeacon !== this.config.itach_beacon) {
      WS.request("set_ir_config", {
        itach_emulation: itachEmulation,
        itach_beacon: itachBeacon
      }).then(function(data) {
        self.config.itach_emulation = itachEmulation;
        self.config.itach_beacon = itachBeacon;
      }).catch(function(e) {
        Toast.error(e);
      });
    }

    // Apply serial TCP config if changed
    if (serialTcp !== this.config.serial_tcp) {
      WS.request("set_serial_tcp", { enable: serialTcp })
          .then(function() {
            self.config.serial_tcp = serialTcp;
          })
          .catch(function(e) {
            Toast.error(e);
          });
    }

    // Show saved message (reboot message is shown globally in WS.onMessage)
    setTimeout(function() {
      Toast.success(I18N.t("t_expert_saved"));
    }, 500);
  },

  init: function() {
    var self = this;

    document.getElementById("btn-expert-apply").addEventListener("click", function() {
      self.apply();
    });
  }
};

// ============================================================
// Application Initialization
// ============================================================
document.addEventListener("DOMContentLoaded", function() {
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
