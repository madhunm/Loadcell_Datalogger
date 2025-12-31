/**
 * Loadcell Datalogger WebUI - Shared JavaScript Utilities
 */

// ============================================================================
// API Configuration
// ============================================================================

const API = {
    // Base URL (empty for relative paths on ESP32, or localhost for dev)
    baseUrl: '',
    
    // Check if running in demo mode (local dev server)
    isDemoMode: false,
    
    /**
     * Initialize API - detect demo mode
     */
    init() {
        // Check for demo mode by trying to detect local development
        this.isDemoMode = window.location.hostname === 'localhost' || 
                          window.location.hostname === '127.0.0.1';
        
        if (this.isDemoMode) {
            console.log('[API] Running in Demo Mode');
            this.showDemoBanner();
        }
    },
    
    /**
     * Show demo mode banner
     */
    showDemoBanner() {
        const banner = document.createElement('div');
        banner.className = 'demo-banner';
        banner.textContent = '⚠ DEMO MODE - Not connected to hardware';
        document.body.insertBefore(banner, document.body.firstChild);
    },
    
    /**
     * Make API request
     */
    async request(endpoint, options = {}) {
        const url = this.baseUrl + endpoint;
        
        const defaultOptions = {
            headers: {
                'Content-Type': 'application/json'
            }
        };
        
        const mergedOptions = { ...defaultOptions, ...options };
        if (options.body && typeof options.body === 'object') {
            mergedOptions.body = JSON.stringify(options.body);
        }
        
        try {
            const response = await fetch(url, mergedOptions);
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }
            
            return await response.json();
        } catch (error) {
            console.error(`[API] Request failed: ${endpoint}`, error);
            throw error;
        }
    },
    
    // Convenience methods
    get(endpoint) {
        return this.request(endpoint, { method: 'GET' });
    },
    
    post(endpoint, data) {
        return this.request(endpoint, { method: 'POST', body: data });
    }
};

// ============================================================================
// Mode Manager
// ============================================================================

const Mode = {
    current: 'user',
    
    /**
     * Get current mode from API
     */
    async fetch() {
        try {
            const data = await API.get('/api/mode');
            this.current = data.mode;
            this.updateUI();
            return this.current;
        } catch (error) {
            console.error('[Mode] Failed to fetch mode:', error);
            return this.current;
        }
    },
    
    /**
     * Switch to a new mode
     */
    async switchTo(mode, password = null) {
        try {
            const data = await API.post('/api/mode', { mode, password });
            
            if (data.success) {
                this.current = mode;
                this.updateUI();
                return true;
            } else {
                UI.showAlert(data.error || 'Failed to switch mode', 'error');
                return false;
            }
        } catch (error) {
            console.error('[Mode] Failed to switch mode:', error);
            UI.showAlert('Failed to switch mode', 'error');
            return false;
        }
    },
    
    /**
     * Update UI elements based on current mode
     */
    updateUI() {
        // Update mode badge
        const badges = document.querySelectorAll('.mode-badge');
        badges.forEach(badge => {
            badge.className = `mode-badge mode-badge--${this.current}`;
            badge.textContent = this.current.toUpperCase();
        });
        
        // Update nav links
        const navLinks = document.querySelectorAll('.nav__link');
        navLinks.forEach(link => {
            const href = link.getAttribute('href');
            link.classList.toggle('nav__link--active', href === this.getPageForMode());
        });
    },
    
    /**
     * Get the page URL for current mode
     */
    getPageForMode() {
        switch (this.current) {
            case 'factory': return 'factory.html';
            case 'admin': return 'admin.html';
            default: return 'user.html';
        }
    },
    
    /**
     * Check if current mode requires password to leave
     */
    requiresPasswordToSwitch() {
        return this.current !== 'user';
    }
};

// ============================================================================
// UI Utilities
// ============================================================================

const UI = {
    /**
     * Show an alert message
     */
    showAlert(message, type = 'info') {
        const alertDiv = document.createElement('div');
        alertDiv.className = `alert alert--${type}`;
        alertDiv.innerHTML = `
            <span>${message}</span>
            <button onclick="this.parentElement.remove()" style="margin-left: auto; background: none; border: none; color: inherit; cursor: pointer;">✕</button>
        `;
        
        const container = document.querySelector('.container') || document.body;
        container.insertBefore(alertDiv, container.firstChild);
        
        // Auto-remove after 5 seconds
        setTimeout(() => alertDiv.remove(), 5000);
    },
    
    /**
     * Show modal dialog
     */
    showModal(title, content, onConfirm) {
        const overlay = document.createElement('div');
        overlay.className = 'modal-overlay';
        overlay.innerHTML = `
            <div class="modal">
                <h3 class="modal__title">${title}</h3>
                <div class="modal__content">${content}</div>
                <div class="modal__actions">
                    <button class="btn btn--secondary" data-action="cancel">Cancel</button>
                    <button class="btn btn--primary" data-action="confirm">Confirm</button>
                </div>
            </div>
        `;
        
        document.body.appendChild(overlay);
        requestAnimationFrame(() => overlay.classList.add('modal-overlay--visible'));
        
        const closeModal = () => {
            overlay.classList.remove('modal-overlay--visible');
            setTimeout(() => overlay.remove(), 250);
        };
        
        overlay.querySelector('[data-action="cancel"]').onclick = closeModal;
        overlay.querySelector('[data-action="confirm"]').onclick = () => {
            if (onConfirm) onConfirm();
            closeModal();
        };
        overlay.onclick = (e) => {
            if (e.target === overlay) closeModal();
        };
        
        return { close: closeModal };
    },
    
    /**
     * Show password prompt modal
     */
    showPasswordPrompt(title, onSubmit) {
        const content = `
            <div class="form-group">
                <label class="form-label">Password</label>
                <input type="password" class="form-input" id="modal-password" placeholder="Enter password">
            </div>
        `;
        
        const modal = this.showModal(title, content, () => {
            const password = document.getElementById('modal-password').value;
            onSubmit(password);
        });
        
        // Focus password input
        setTimeout(() => {
            document.getElementById('modal-password').focus();
        }, 100);
        
        return modal;
    },
    
    /**
     * Format file size
     */
    formatSize(bytes) {
        const units = ['B', 'KB', 'MB', 'GB'];
        let i = 0;
        while (bytes >= 1024 && i < units.length - 1) {
            bytes /= 1024;
            i++;
        }
        return `${bytes.toFixed(1)} ${units[i]}`;
    },
    
    /**
     * Format percentage
     */
    formatPercent(value, decimals = 0) {
        return `${value.toFixed(decimals)}%`;
    },
    
    /**
     * Update element text content safely
     */
    setText(selector, text) {
        const el = document.querySelector(selector);
        if (el) el.textContent = text;
    },
    
    /**
     * Update progress bar
     */
    setProgress(selector, percent, variant = '') {
        const bar = document.querySelector(selector);
        if (!bar) return;
        
        const fill = bar.querySelector('.progress-bar__fill') || bar;
        fill.style.width = `${Math.min(100, Math.max(0, percent))}%`;
        
        if (variant) {
            fill.className = `progress-bar__fill progress-bar__fill--${variant}`;
        }
    },
    
    /**
     * Set loading state on button
     */
    setButtonLoading(button, loading) {
        if (loading) {
            button.disabled = true;
            button.dataset.originalText = button.textContent;
            button.innerHTML = '<span class="spinner"></span> Loading...';
        } else {
            button.disabled = false;
            button.textContent = button.dataset.originalText || 'Submit';
        }
    }
};

// ============================================================================
// Status Updates
// ============================================================================

const Status = {
    pollInterval: null,
    
    /**
     * Start polling for status updates
     */
    startPolling(intervalMs = 2000) {
        this.stopPolling();
        this.update();
        this.pollInterval = setInterval(() => this.update(), intervalMs);
    },
    
    /**
     * Stop polling
     */
    stopPolling() {
        if (this.pollInterval) {
            clearInterval(this.pollInterval);
            this.pollInterval = null;
        }
    },
    
    /**
     * Fetch and update status
     */
    async update() {
        try {
            const status = await API.get('/api/status');
            this.render(status);
            return status;
        } catch (error) {
            console.error('[Status] Update failed:', error);
            return null;
        }
    },
    
    /**
     * Render status to UI (override in page-specific JS)
     */
    render(status) {
        // Update mode if changed
        if (status.mode && status.mode !== Mode.current) {
            Mode.current = status.mode;
            Mode.updateUI();
        }
        
        // Dispatch event for page-specific handlers
        document.dispatchEvent(new CustomEvent('statusUpdate', { detail: status }));
    }
};

// ============================================================================
// Initialization
// ============================================================================

document.addEventListener('DOMContentLoaded', () => {
    API.init();
    Mode.fetch();
});

// Export for use in other scripts
window.API = API;
window.Mode = Mode;
window.UI = UI;
window.Status = Status;

