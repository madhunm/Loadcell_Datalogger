/**
 * Admin Page - Field Configuration and Calibration
 */

let calibrationPoints = [];
let liveUpdateInterval = null;

// Initialize calibration table with default points
function initCalibrationTable() {
    calibrationPoints = [
        { load_kg: 0, output_uV: 0 },
        { load_kg: 500, output_uV: 2500 },
        { load_kg: 1000, output_uV: 5000 },
        { load_kg: 1500, output_uV: 7500 },
        { load_kg: 2000, output_uV: 10000 }
    ];
    renderCalibrationTable();
}

function renderCalibrationTable() {
    const tbody = document.getElementById('calibration-table');
    tbody.innerHTML = calibrationPoints.map((point, i) => `
        <tr>
            <td>${i + 1}</td>
            <td>
                <input type="number" class="form-input" value="${point.load_kg}" 
                       onchange="updateCalPoint(${i}, 'load_kg', this.value)" step="0.1">
            </td>
            <td>
                <input type="number" class="form-input" value="${point.output_uV}" 
                       onchange="updateCalPoint(${i}, 'output_uV', this.value)" step="0.1">
            </td>
            <td>
                <button class="btn btn--small btn--secondary" onclick="useCurrentReading(${i})">📊 Use Current</button>
                ${i > 1 ? `<button class="btn btn--small btn--danger" onclick="removeCalPoint(${i})">✕</button>` : ''}
            </td>
        </tr>
    `).join('');
}

export function addCalibrationPoint() {
    const lastPoint = calibrationPoints[calibrationPoints.length - 1];
    calibrationPoints.push({
        load_kg: lastPoint.load_kg + 500,
        output_uV: lastPoint.output_uV + 2500
    });
    renderCalibrationTable();
}

export function removeCalPoint(index) {
    if (calibrationPoints.length > 2) {
        calibrationPoints.splice(index, 1);
        renderCalibrationTable();
    }
}

export function updateCalPoint(index, field, value) {
    calibrationPoints[index][field] = parseFloat(value);
}

export async function useCurrentReading(index) {
    try {
        const data = await API.get('/api/live');
        calibrationPoints[index].output_uV = data.raw_adc || 0;
        renderCalibrationTable();
        UI.showAlert(`Point ${index + 1} updated with current reading`, 'success');
    } catch (error) {
        UI.showAlert('Failed to read current value', 'error');
    }
}

export async function readCurrentAdc() {
    try {
        const data = await API.get('/api/live');
        document.getElementById('live-adc').textContent = data.raw_adc?.toLocaleString() || '--';
        document.getElementById('live-uv').textContent = (data.raw_adc / 100)?.toFixed(2) + ' µV' || '--';
    } catch (error) {
        UI.showAlert('Failed to read ADC', 'error');
    }
}

export async function autoZero() {
    UI.showModal('Auto Zero', 
        '<p>Remove all load from the loadcell and click Confirm to set zero point.</p>',
        async () => {
            try {
                const data = await API.get('/api/live');
                calibrationPoints[0] = { load_kg: 0, output_uV: data.raw_adc || 0 };
                renderCalibrationTable();
                UI.showAlert('Zero point set successfully', 'success');
            } catch (error) {
                UI.showAlert('Failed to auto-zero', 'error');
            }
        }
    );
}

// Load configuration from device
export async function loadConfig() {
    try {
        const config = await API.get('/api/config');
        
        // Populate form fields
        document.getElementById('cfg-loadcell-id').value = config.loadcell_id || '';
        document.getElementById('cfg-loadcell-model').value = config.loadcell_model || '';
        document.getElementById('cfg-loadcell-serial').value = config.loadcell_serial || '';
        document.getElementById('cfg-capacity').value = config.capacity_kg || '';
        document.getElementById('cfg-excitation').value = config.excitation_V || '';
        document.getElementById('cfg-sensitivity').value = config.sensitivity_mVV || '';
        document.getElementById('cfg-pga-gain').value = config.adc_pga_gain || 128;
        document.getElementById('cfg-imu-g-range').value = config.imu_g_range || 16;
        document.getElementById('cfg-imu-gyro-dps').value = config.imu_gyro_dps || 2000;
        
        // Load calibration points
        if (config.calibration_points && config.calibration_points.length > 0) {
            calibrationPoints = config.calibration_points;
            renderCalibrationTable();
        }
        
        UI.showAlert('Configuration loaded', 'success');
    } catch (error) {
        UI.showAlert('Failed to load configuration', 'error');
    }
}

// Save configuration to device
export async function saveConfig() {
    const config = {
        loadcell_id: document.getElementById('cfg-loadcell-id').value,
        loadcell_model: document.getElementById('cfg-loadcell-model').value,
        loadcell_serial: document.getElementById('cfg-loadcell-serial').value,
        capacity_kg: parseFloat(document.getElementById('cfg-capacity').value) || 2000,
        excitation_V: parseFloat(document.getElementById('cfg-excitation').value) || 10,
        sensitivity_mVV: parseFloat(document.getElementById('cfg-sensitivity').value) || 0,
        adc_pga_gain: parseInt(document.getElementById('cfg-pga-gain').value),
        imu_g_range: parseInt(document.getElementById('cfg-imu-g-range').value),
        imu_gyro_dps: parseInt(document.getElementById('cfg-imu-gyro-dps').value),
        calibration_points: calibrationPoints
    };
    
    try {
        await API.post('/api/config', config);
        UI.showAlert('Configuration saved to device', 'success');
    } catch (error) {
        UI.showAlert('Failed to save configuration', 'error');
    }
}

// Export configuration as JSON
export function exportConfig() {
    const config = {
        loadcell_id: document.getElementById('cfg-loadcell-id').value,
        loadcell_model: document.getElementById('cfg-loadcell-model').value,
        loadcell_serial: document.getElementById('cfg-loadcell-serial').value,
        capacity_kg: parseFloat(document.getElementById('cfg-capacity').value),
        excitation_V: parseFloat(document.getElementById('cfg-excitation').value),
        adc_pga_gain: parseInt(document.getElementById('cfg-pga-gain').value),
        imu_g_range: parseInt(document.getElementById('cfg-imu-g-range').value),
        imu_gyro_dps: parseInt(document.getElementById('cfg-imu-gyro-dps').value),
        calibration_points: calibrationPoints
    };
    
    const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `loadcell_config_${config.loadcell_id || 'export'}.json`;
    a.click();
    URL.revokeObjectURL(url);
}

// Import configuration from JSON
export function importConfig() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;
        
        try {
            const text = await file.text();
            const config = JSON.parse(text);
            
            // Populate form
            if (config.loadcell_id) document.getElementById('cfg-loadcell-id').value = config.loadcell_id;
            if (config.loadcell_model) document.getElementById('cfg-loadcell-model').value = config.loadcell_model;
            if (config.loadcell_serial) document.getElementById('cfg-loadcell-serial').value = config.loadcell_serial;
            if (config.capacity_kg) document.getElementById('cfg-capacity').value = config.capacity_kg;
            if (config.excitation_V) document.getElementById('cfg-excitation').value = config.excitation_V;
            if (config.adc_pga_gain) document.getElementById('cfg-pga-gain').value = config.adc_pga_gain;
            if (config.imu_g_range) document.getElementById('cfg-imu-g-range').value = config.imu_g_range;
            if (config.imu_gyro_dps) document.getElementById('cfg-imu-gyro-dps').value = config.imu_gyro_dps;
            
            if (config.calibration_points) {
                calibrationPoints = config.calibration_points;
                renderCalibrationTable();
            }
            
            UI.showAlert('Configuration imported', 'success');
        } catch (error) {
            UI.showAlert('Failed to import: Invalid JSON', 'error');
        }
    };
    input.click();
}

// SSE Event Source for real-time streaming
let eventSource = null;

function handleStreamData(data) {
    document.getElementById('live-adc').textContent = data.adc?.toLocaleString() || '--';
    document.getElementById('live-uv').textContent = (data.uV || 0).toFixed(1) + ' µV';
    document.getElementById('live-ax').textContent = (data.ax?.toFixed(3) || '--') + ' g';
    document.getElementById('live-ay').textContent = (data.ay?.toFixed(3) || '--') + ' g';
    document.getElementById('live-az').textContent = (data.az?.toFixed(3) || '--') + ' g';
    document.getElementById('live-gx').textContent = (data.gx?.toFixed(1) || '--') + ' dps';
    document.getElementById('live-gy').textContent = (data.gy?.toFixed(1) || '--') + ' dps';
    document.getElementById('live-gz').textContent = (data.gz?.toFixed(1) || '--') + ' dps';
}

function startLiveUpdates() {
    if (eventSource) {
        eventSource.close();
    }
    
    eventSource = new EventSource('/api/stream');
    
    eventSource.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            handleStreamData(data);
        } catch (e) {
            console.error('SSE parse error:', e);
        }
    };
    
    eventSource.onerror = () => {
        console.log('SSE connection lost, reconnecting...');
        eventSource.close();
        eventSource = null;
        setTimeout(startLiveUpdates, 1000);
    };
    
    eventSource.onopen = () => {
        console.log('SSE stream connected');
    };
}

function stopLiveUpdates() {
    if (eventSource) {
        eventSource.close();
        eventSource = null;
    }
}

// OTA Firmware upload
export async function uploadFirmware() {
    const fileInput = document.getElementById('ota-file');
    const file = fileInput.files[0];
    
    if (!file) {
        UI.showAlert('Please select a firmware file', 'warning');
        return;
    }
    
    if (!file.name.endsWith('.bin')) {
        UI.showAlert('Invalid file type. Please select a .bin file', 'error');
        return;
    }
    
    const btn = document.getElementById('btn-ota-upload');
    const progressContainer = document.getElementById('ota-progress-container');
    const progressBar = document.getElementById('ota-progress');
    const statusDiv = document.getElementById('ota-status');
    
    btn.disabled = true;
    btn.textContent = 'Uploading...';
    progressContainer.style.display = 'block';
    statusDiv.textContent = 'Starting upload...';
    
    try {
        const xhr = new XMLHttpRequest();
        
        xhr.upload.addEventListener('progress', (e) => {
            if (e.lengthComputable) {
                const percent = Math.round((e.loaded / e.total) * 100);
                progressBar.style.width = percent + '%';
                statusDiv.textContent = `Uploading: ${percent}%`;
            }
        });
        
        xhr.onload = function() {
            if (xhr.status === 200) {
                const result = JSON.parse(xhr.responseText);
                if (result.success) {
                    statusDiv.textContent = 'Update complete! Restarting device...';
                    statusDiv.style.color = 'var(--accent-green)';
                    UI.showAlert('Firmware update successful! Device is restarting...', 'success');
                    
                    // Redirect after device restarts
                    setTimeout(() => {
                        location.href = 'index.html';
                    }, 5000);
                } else {
                    throw new Error(result.error || 'Update failed');
                }
            } else {
                throw new Error('Upload failed: ' + xhr.statusText);
            }
        };
        
        xhr.onerror = function() {
            throw new Error('Network error during upload');
        };
        
        xhr.open('POST', '/api/ota', true);
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.send(file);
        
    } catch (error) {
        statusDiv.textContent = 'Error: ' + error.message;
        statusDiv.style.color = 'var(--accent-red)';
        UI.showAlert('Firmware update failed: ' + error.message, 'error');
        btn.disabled = false;
        btn.textContent = '📤 Upload & Install Firmware';
    }
}

// Exit admin mode
export async function exitAdminMode() {
    stopLiveUpdates();
    const success = await Mode.switchTo('user');
    if (success) {
        location.href = 'index.html';
    }
}

// Initialize
function init() {
    initCalibrationTable();
    loadConfig();
    startLiveUpdates();
    
    // Verify we're in admin mode
    Mode.fetch().then(mode => {
        if (mode !== 'admin') {
            UI.showAlert('Not in Admin mode. Please switch modes from the main page.', 'warning');
        }
    });
}

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
    stopLiveUpdates();
});

// Expose functions globally for onclick handlers
window.addCalibrationPoint = addCalibrationPoint;
window.removeCalPoint = removeCalPoint;
window.updateCalPoint = updateCalPoint;
window.useCurrentReading = useCurrentReading;
window.readCurrentAdc = readCurrentAdc;
window.autoZero = autoZero;
window.loadConfig = loadConfig;
window.saveConfig = saveConfig;
window.exportConfig = exportConfig;
window.importConfig = importConfig;
window.uploadFirmware = uploadFirmware;
window.exitAdminMode = exitAdminMode;

// Initialize on DOM ready
document.addEventListener('DOMContentLoaded', init);

