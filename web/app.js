// Loadcell Datalogger WebUI - Core Utilities

const API = {
    baseUrl: '',
    isDemoMode: false,
    
    init() {
        this.isDemoMode = window.location.hostname === 'localhost' || window.location.hostname === '127.0.0.1';
        if (this.isDemoMode) {
            console.log('[API] Demo Mode');
            const b = document.createElement('div');
            b.className = 'demo-banner';
            b.textContent = '⚠ DEMO MODE - Not connected to hardware';
            document.body.insertBefore(b, document.body.firstChild);
        }
    },
    
    async request(endpoint, options = {}) {
        const url = this.baseUrl + endpoint;
        const opts = { headers: { 'Content-Type': 'application/json' }, ...options };
        if (options.body && typeof options.body === 'object') opts.body = JSON.stringify(options.body);
        
        try {
            const res = await fetch(url, opts);
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            return await res.json();
        } catch (e) {
            console.error(`[API] ${endpoint}`, e);
            throw e;
        }
    },
    
    get(endpoint) { return this.request(endpoint, { method: 'GET' }); },
    post(endpoint, data) { return this.request(endpoint, { method: 'POST', body: data }); }
};

const Mode = {
    current: 'user',
    
    async fetch() {
        try {
            const data = await API.get('/api/mode');
            this.current = data.mode;
            this.updateUI();
            return this.current;
        } catch (e) {
            return this.current;
        }
    },
    
    async switchTo(mode, password = null) {
        try {
            const data = await API.post('/api/mode', { mode, password });
            if (data.success) {
                this.current = mode;
                this.updateUI();
                return true;
            }
            UI.showAlert(data.error || 'Failed to switch mode', 'error');
            return false;
        } catch (e) {
            UI.showAlert('Failed to switch mode', 'error');
            return false;
        }
    },
    
    updateUI() {
        document.querySelectorAll('.mode-badge').forEach(b => {
            b.className = `mode-badge mode-badge--${this.current}`;
            b.textContent = this.current.toUpperCase();
        });
    },
    
    getPageForMode() {
        return this.current === 'factory' ? 'factory.html' : this.current === 'admin' ? 'admin.html' : 'user.html';
    }
};

const UI = {
    showAlert(message, type = 'info') {
        const d = document.createElement('div');
        d.className = `alert alert--${type}`;
        d.innerHTML = `<span>${message}</span><button onclick="this.parentElement.remove()" style="margin-left:auto;background:none;border:none;color:inherit;cursor:pointer;">✕</button>`;
        const c = document.querySelector('.container') || document.body;
        c.insertBefore(d, c.firstChild);
        setTimeout(() => d.remove(), 5000);
    },
    
    showModal(title, content, onConfirm) {
        const o = document.createElement('div');
        o.className = 'modal-overlay';
        o.innerHTML = `<div class="modal"><h3 class="modal__title">${title}</h3><div class="modal__content">${content}</div><div class="modal__actions"><button class="btn btn--secondary" data-action="cancel">Cancel</button><button class="btn btn--primary" data-action="confirm">Confirm</button></div></div>`;
        document.body.appendChild(o);
        requestAnimationFrame(() => o.classList.add('modal-overlay--visible'));
        
        const close = () => { o.classList.remove('modal-overlay--visible'); setTimeout(() => o.remove(), 200); };
        o.querySelector('[data-action="cancel"]').onclick = close;
        o.querySelector('[data-action="confirm"]').onclick = () => { if (onConfirm) onConfirm(); close(); };
        o.onclick = (e) => { if (e.target === o) close(); };
        return { close };
    },
    
    showPasswordPrompt(title, onSubmit) {
        const content = `<div class="form-group"><label class="form-label">Password</label><input type="password" class="form-input" id="modal-password" placeholder="Enter password"></div>`;
        const m = this.showModal(title, content, () => onSubmit(document.getElementById('modal-password').value));
        setTimeout(() => document.getElementById('modal-password').focus(), 100);
        return m;
    },
    
    formatSize(bytes) {
        const u = ['B', 'KB', 'MB', 'GB'];
        let i = 0;
        while (bytes >= 1024 && i < u.length - 1) { bytes /= 1024; i++; }
        return `${bytes.toFixed(1)} ${u[i]}`;
    },
    
    setText(sel, text) { const el = document.querySelector(sel); if (el) el.textContent = text; },
    
    setProgress(sel, pct, variant = '') {
        const bar = document.querySelector(sel);
        if (!bar) return;
        const fill = bar.querySelector('.progress-bar__fill') || bar;
        fill.style.width = `${Math.min(100, Math.max(0, pct))}%`;
        if (variant) fill.className = `progress-bar__fill progress-bar__fill--${variant}`;
    },
    
    setButtonLoading(btn, loading) {
        if (loading) {
            btn.disabled = true;
            btn.dataset.originalText = btn.textContent;
            btn.innerHTML = '<span class="spinner"></span> Loading...';
        } else {
            btn.disabled = false;
            btn.textContent = btn.dataset.originalText || 'Submit';
        }
    }
};

const Status = {
    pollInterval: null,
    
    startPolling(ms = 2000) {
        this.stopPolling();
        this.update();
        this.pollInterval = setInterval(() => this.update(), ms);
    },
    
    stopPolling() {
        if (this.pollInterval) { clearInterval(this.pollInterval); this.pollInterval = null; }
    },
    
    async update() {
        try {
            const s = await API.get('/api/status');
            this.render(s);
            return s;
        } catch (e) {
            return null;
        }
    },
    
    render(status) {
        if (status.mode && status.mode !== Mode.current) {
            Mode.current = status.mode;
            Mode.updateUI();
        }
        document.dispatchEvent(new CustomEvent('statusUpdate', { detail: status }));
    }
};

document.addEventListener('DOMContentLoaded', () => { API.init(); Mode.fetch(); });

window.API = API;
window.Mode = Mode;
window.UI = UI;
window.Status = Status;
