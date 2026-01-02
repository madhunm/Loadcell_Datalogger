/**
 * Index Page - Mode Selection and Quick Actions
 */

let isLogging = false;

// Switch mode with password prompt if needed
export async function switchMode(mode) {
    if (mode === 'user') {
        // User mode doesn't need password
        const success = await Mode.switchTo(mode);
        if (success) {
            location.href = 'user.html';
        }
    } else {
        // Admin and Factory modes need password
        UI.showPasswordPrompt(
            `Enter ${mode === 'admin' ? 'Admin' : 'Factory'} Password`,
            async (password) => {
                const success = await Mode.switchTo(mode, password);
                if (success) {
                    location.href = mode === 'admin' ? 'admin.html' : 'factory.html';
                }
            }
        );
    }
}

// Toggle logging state
export async function toggleLogging() {
    const btn = document.getElementById('btn-start-log');
    UI.setButtonLoading(btn, true);
    
    try {
        const endpoint = isLogging ? '/api/logging/stop' : '/api/logging/start';
        await API.post(endpoint, {});
        isLogging = !isLogging;
        updateLoggingButton();
    } catch (error) {
        UI.showAlert('Failed to toggle logging', 'error');
    } finally {
        UI.setButtonLoading(btn, false);
    }
}

function updateLoggingButton() {
    const btn = document.getElementById('btn-start-log');
    if (isLogging) {
        btn.textContent = '⏹ Stop Logging';
        btn.className = 'btn btn--danger';
    } else {
        btn.textContent = '▶ Start Logging';
        btn.className = 'btn btn--success';
    }
}

// Recovery modal functions
async function checkForRecovery() {
    try {
        const response = await fetch('/api/recovery/status');
        const data = await response.json();
        if (data.has_recovery) {
            document.getElementById('recovery-modal').style.display = 'flex';
        }
    } catch (e) {
        console.error('Failed to check recovery status:', e);
    }
}

export function hideRecoveryModal() {
    document.getElementById('recovery-modal').style.display = 'none';
}

export async function recoverSession() {
    try {
        await API.post('/api/recovery/recover', {});
        UI.showAlert('Session recovered! Resuming logging...', 'success');
        hideRecoveryModal();
        // Redirect to user page where logging is happening
        setTimeout(() => location.href = 'user.html', 1000);
    } catch (e) {
        UI.showAlert('Failed to recover session', 'error');
    }
}

export async function clearRecovery() {
    try {
        await API.post('/api/recovery/clear', {});
        UI.showAlert('Recovery data cleared', 'info');
        hideRecoveryModal();
    } catch (e) {
        UI.showAlert('Failed to clear recovery data', 'error');
    }
}

// Refresh status
export async function refreshStatus() {
    const status = await Status.update();
    if (status) {
        updateStatusDisplay(status);
    }
}

// Update status display
function updateStatusDisplay(status) {
    // Mode
    document.getElementById('stat-mode').textContent = status.mode?.toUpperCase() || 'USER';
    document.getElementById('current-mode').textContent = status.mode?.toUpperCase() || 'USER';
    document.getElementById('current-mode').className = `mode-badge mode-badge--${status.mode || 'user'}`;
    
    // WiFi
    document.getElementById('stat-wifi').textContent = status.wifi ? 'ON' : 'OFF';
    
    // Logging
    isLogging = status.logging;
    document.getElementById('stat-logging').textContent = status.logging ? 'ACTIVE' : 'IDLE';
    document.getElementById('stat-logging').style.color = status.logging ? 'var(--accent-orange)' : 'var(--text-primary)';
    updateLoggingButton();
    
    // SD Card - show used/total capacity
    if (status.sd_present && status.sd_total_mb > 0) {
        const usedGB = (status.sd_used_mb / 1024).toFixed(1);
        const totalGB = (status.sd_total_mb / 1024).toFixed(1);
        document.getElementById('stat-sd').textContent = `${usedGB}/${totalGB} GB`;
        document.getElementById('stat-sd').style.color = 'var(--accent-green)';
    } else {
        document.getElementById('stat-sd').textContent = 'MISSING';
        document.getElementById('stat-sd').style.color = 'var(--accent-red)';
    }
    
    // Device info
    if (status.uptime_ms) {
        const seconds = Math.floor(status.uptime_ms / 1000);
        const minutes = Math.floor(seconds / 60);
        const hours = Math.floor(minutes / 60);
        document.getElementById('info-uptime').textContent = 
            `${hours}h ${minutes % 60}m ${seconds % 60}s`;
    }
    
    if (status.free_heap) {
        document.getElementById('info-heap').textContent = 
            `${(status.free_heap / 1024).toFixed(1)} KB`;
    }
    
    // Demo mode indicator
    if (status.demo_mode) {
        document.getElementById('status-text').textContent = 'Demo Mode';
        document.getElementById('status-dot').style.background = 'var(--accent-orange)';
    }
}

// Initialize page
function init() {
    // Listen for status updates
    document.addEventListener('statusUpdate', (e) => {
        updateStatusDisplay(e.detail);
    });

    // Start polling when page loads
    Status.startPolling(3000);
    
    // Check for recoverable session
    checkForRecovery();
}

// Expose functions globally for onclick handlers
window.switchMode = switchMode;
window.toggleLogging = toggleLogging;
window.refreshStatus = refreshStatus;
window.hideRecoveryModal = hideRecoveryModal;
window.recoverSession = recoverSession;
window.clearRecovery = clearRecovery;

// Initialize on DOM ready
document.addEventListener('DOMContentLoaded', init);

