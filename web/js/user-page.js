/**
 * User Page - Dashboard and Live Data Display
 */

// Chart data
const chartData = {
    timestamps: [],
    values: [],
    maxPoints: 300,  // 30 seconds at 10Hz
    timespan: 30
};

let isLogging = false;
let liveUpdateInterval = null;
let chartCtx = null;

// Simple canvas-based chart (no external libraries)
function initChart() {
    const canvas = document.getElementById('load-chart');
    chartCtx = canvas.getContext('2d');
    
    // Set canvas resolution
    const rect = canvas.parentElement.getBoundingClientRect();
    canvas.width = rect.width * window.devicePixelRatio;
    canvas.height = rect.height * window.devicePixelRatio;
    chartCtx.scale(window.devicePixelRatio, window.devicePixelRatio);
    
    drawChart();
}

function drawChart() {
    if (!chartCtx) return;
    
    const canvas = chartCtx.canvas;
    const width = canvas.width / window.devicePixelRatio;
    const height = canvas.height / window.devicePixelRatio;
    
    // Clear
    chartCtx.fillStyle = '#0f0f1a';
    chartCtx.fillRect(0, 0, width, height);
    
    if (chartData.values.length < 2) {
        document.getElementById('chart-placeholder').style.display = 'flex';
        return;
    }
    
    document.getElementById('chart-placeholder').style.display = 'none';
    
    // Calculate bounds
    const padding = { top: 20, right: 20, bottom: 30, left: 60 };
    const chartWidth = width - padding.left - padding.right;
    const chartHeight = height - padding.top - padding.bottom;
    
    const minVal = Math.min(...chartData.values) - 10;
    const maxVal = Math.max(...chartData.values) + 10;
    const valRange = maxVal - minVal || 1;
    
    // Draw grid
    chartCtx.strokeStyle = '#2a2a40';
    chartCtx.lineWidth = 1;
    
    // Horizontal grid lines
    for (let i = 0; i <= 4; i++) {
        const y = padding.top + (chartHeight * i / 4);
        chartCtx.beginPath();
        chartCtx.moveTo(padding.left, y);
        chartCtx.lineTo(width - padding.right, y);
        chartCtx.stroke();
        
        // Y-axis labels
        const val = maxVal - (valRange * i / 4);
        chartCtx.fillStyle = '#606070';
        chartCtx.font = '11px sans-serif';
        chartCtx.textAlign = 'right';
        chartCtx.fillText(val.toFixed(1), padding.left - 5, y + 4);
    }
    
    // Draw line
    chartCtx.beginPath();
    chartCtx.strokeStyle = '#00ff6a';
    chartCtx.lineWidth = 2;
    
    chartData.values.forEach((val, i) => {
        const x = padding.left + (chartWidth * i / (chartData.values.length - 1));
        const y = padding.top + chartHeight - ((val - minVal) / valRange * chartHeight);
        
        if (i === 0) {
            chartCtx.moveTo(x, y);
        } else {
            chartCtx.lineTo(x, y);
        }
    });
    
    chartCtx.stroke();
    
    // Draw gradient fill under line
    const gradient = chartCtx.createLinearGradient(0, padding.top, 0, height - padding.bottom);
    gradient.addColorStop(0, 'rgba(0, 255, 106, 0.3)');
    gradient.addColorStop(1, 'rgba(0, 255, 106, 0)');
    
    chartCtx.lineTo(width - padding.right, height - padding.bottom);
    chartCtx.lineTo(padding.left, height - padding.bottom);
    chartCtx.closePath();
    chartCtx.fillStyle = gradient;
    chartCtx.fill();
    
    // Y-axis label
    chartCtx.save();
    chartCtx.translate(15, height / 2);
    chartCtx.rotate(-Math.PI / 2);
    chartCtx.fillStyle = '#9090a0';
    chartCtx.textAlign = 'center';
    chartCtx.fillText('Load (kg)', 0, 0);
    chartCtx.restore();
}

function addChartPoint(value) {
    chartData.values.push(value);
    chartData.timestamps.push(Date.now());
    
    // Trim old data
    const maxPoints = chartData.timespan * 10;  // Assuming 10Hz update rate
    while (chartData.values.length > maxPoints) {
        chartData.values.shift();
        chartData.timestamps.shift();
    }
    
    drawChart();
}

export function clearChart() {
    chartData.values = [];
    chartData.timestamps = [];
    drawChart();
}

export function setChartTimespan(seconds) {
    chartData.timespan = parseInt(seconds);
    clearChart();
}

// Toggle logging
export async function toggleLogging() {
    const btn = document.getElementById('btn-start-log');
    UI.setButtonLoading(btn, true);
    
    try {
        const endpoint = isLogging ? '/api/logging/stop' : '/api/logging/start';
        await API.post(endpoint, {});
        
        // If we just stopped logging, show session summary
        if (isLogging) {
            setTimeout(showSessionSummary, 500);  // Small delay for final stats
        }
        
        isLogging = !isLogging;
        updateLoggingButton();
    } catch (error) {
        UI.showAlert('Failed to toggle logging', 'error');
    } finally {
        UI.setButtonLoading(btn, false);
    }
}

// Fetch and display session summary
async function showSessionSummary() {
    try {
        const response = await fetch('/api/session/summary');
        const data = await response.json();
        
        if (data.valid) {
            document.getElementById('summary-peak-load').textContent = data.peak_load_n.toFixed(2);
            document.getElementById('summary-peak-load-time').textContent = data.peak_load_time_s.toFixed(3);
            document.getElementById('summary-peak-decel').textContent = data.peak_decel_g.toFixed(2);
            document.getElementById('summary-peak-decel-time').textContent = data.peak_decel_time_s.toFixed(3);
            document.getElementById('summary-duration').textContent = formatDuration(data.duration_s * 1000);
            document.getElementById('summary-adc-samples').textContent = formatNumber(data.total_adc_samples);
            document.getElementById('summary-imu-samples').textContent = formatNumber(data.total_imu_samples);
            document.getElementById('summary-dropped').textContent = data.dropped_samples;
            
            document.getElementById('session-summary').style.display = 'block';
        }
    } catch (error) {
        console.error('Failed to fetch session summary:', error);
    }
}

export function hideSessionSummary() {
    document.getElementById('session-summary').style.display = 'none';
}

function formatNumber(num) {
    if (num >= 1000000) return (num / 1000000).toFixed(2) + 'M';
    if (num >= 1000) return (num / 1000).toFixed(1) + 'K';
    return num.toString();
}

function formatDuration(ms) {
    const s = Math.floor(ms / 1000);
    const m = Math.floor(s / 60);
    const h = Math.floor(m / 60);
    if (h > 0) return `${h}h ${m % 60}m ${s % 60}s`;
    if (m > 0) return `${m}m ${s % 60}s`;
    return `${s}s`;
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

// SSE Event Source for real-time streaming
let eventSource = null;

function handleStreamData(data) {
    // Update live values
    const loadKg = (data.uV || 0) / 10.0;  // Simple conversion
    document.getElementById('live-load').textContent = loadKg.toFixed(2);
    document.getElementById('live-adc').textContent = (data.adc || 0).toLocaleString();
    
    // Display ADC sample rate in kSPS (e.g., 64000 → "64.0k")
    const sampleRateHz = data.sample_rate_hz || 0;
    if (sampleRateHz >= 1000) {
        document.getElementById('live-rate').textContent = (sampleRateHz / 1000).toFixed(1) + 'k';
    } else {
        document.getElementById('live-rate').textContent = sampleRateHz.toString();
    }
    
    // Update IMU
    document.getElementById('imu-ax').textContent = (data.ax || 0).toFixed(3);
    document.getElementById('imu-ay').textContent = (data.ay || 0).toFixed(3);
    document.getElementById('imu-az').textContent = (data.az || 1).toFixed(3);
    document.getElementById('imu-gx').textContent = (data.gx || 0).toFixed(1);
    document.getElementById('imu-gy').textContent = (data.gy || 0).toFixed(1);
    document.getElementById('imu-gz').textContent = (data.gz || 0).toFixed(1);
    
    // Update integrity indicator when logging
    const integrityDiv = document.getElementById('integrity-status');
    if (data.logging) {
        integrityDiv.style.display = 'block';
        
        // Update buffer fill
        const bufPct = data.buf_pct || 0;
        const bufEl = document.getElementById('integrity-buffer');
        bufEl.textContent = bufPct.toFixed(0) + '%';
        bufEl.style.color = bufPct < 50 ? '#22c55e' : (bufPct < 80 ? '#f59e0b' : '#ef4444');
        
        // Update latency
        const latencyMs = (data.latency_us || 0) / 1000;
        const latencyEl = document.getElementById('integrity-latency');
        latencyEl.textContent = latencyMs.toFixed(1) + 'ms';
        latencyEl.style.color = latencyMs < 10 ? '#22c55e' : (latencyMs < 50 ? '#f59e0b' : '#ef4444');
        
        // Update drops
        const drops = data.drops || 0;
        const dropsEl = document.getElementById('integrity-drops');
        dropsEl.textContent = drops.toLocaleString();
        dropsEl.style.color = drops === 0 ? '#22c55e' : '#ef4444';
        
        // Update overall indicator LED
        const led = document.getElementById('integrity-led');
        if (drops > 0 || bufPct > 80 || latencyMs > 50) {
            led.style.background = '#ef4444';  // Red
        } else if (bufPct > 50 || latencyMs > 10) {
            led.style.background = '#f59e0b';  // Yellow
        } else {
            led.style.background = '#22c55e';  // Green
        }
    } else {
        integrityDiv.style.display = 'none';
    }
    
    // Add to chart
    addChartPoint(loadKg);
}

function startStream() {
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
    
    eventSource.onerror = (error) => {
        console.log('SSE connection lost, reconnecting...');
        eventSource.close();
        eventSource = null;
        // Reconnect after 1 second
        setTimeout(startStream, 1000);
    };
    
    eventSource.onopen = () => {
        console.log('SSE stream connected');
    };
}

function stopStream() {
    if (eventSource) {
        eventSource.close();
        eventSource = null;
    }
}

// SD Card stats
async function updateSDCard() {
    try {
        const data = await API.get('/api/sdcard');
        
        const usedPercent = (data.used_mb / data.total_mb * 100) || 0;
        
        document.getElementById('sd-used').textContent = UI.formatSize(data.used_mb * 1024 * 1024);
        document.getElementById('sd-free').textContent = UI.formatSize(data.free_mb * 1024 * 1024) + ' free';
        document.getElementById('sd-total').textContent = 'of ' + UI.formatSize(data.total_mb * 1024 * 1024);
        
        UI.setProgress('#sd-progress', usedPercent, usedPercent > 90 ? 'danger' : usedPercent > 70 ? 'warning' : '');
        
        const statusDot = document.getElementById('sd-status-dot');
        statusDot.className = `status-dot status-dot--${data.present ? 'success' : 'error'}`;
        
        // Fetch file list separately
        await updateFileList();
        
    } catch (error) {
        document.getElementById('sd-used').textContent = 'Error';
    }
}

// Fetch files from /api/files
async function updateFileList() {
    try {
        const data = await API.get('/api/files');
        document.getElementById('sd-file-count').textContent = data.files?.length || 0;
        renderFileList(data.files || []);
    } catch (error) {
        console.error('Failed to fetch files:', error);
        document.getElementById('sd-file-count').textContent = '?';
    }
}

// Battery stats
async function updateBattery() {
    try {
        const data = await API.get('/api/battery');
        
        // Check if battery is present
        if (!data.present) {
            document.getElementById('battery-percent').textContent = 'N/A';
            document.getElementById('battery-voltage').textContent = 'Not detected';
            document.getElementById('battery-charging').classList.add('hidden');
            UI.setProgress('#battery-progress', 0, 'secondary');
            return;
        }
        
        const percent = data.soc_percent || 0;
        document.getElementById('battery-percent').textContent = percent.toFixed(0) || '--';
        document.getElementById('battery-voltage').textContent = (data.voltage_V || 0).toFixed(2) + ' V';
        
        const variant = percent > 50 ? 'success' : percent > 20 ? 'warning' : 'danger';
        UI.setProgress('#battery-progress', percent, variant);
        
        // Charging if charge rate is positive
        const charging = (data.charge_rate_pct_hr || 0) > 0;
        document.getElementById('battery-charging').classList.toggle('hidden', !charging);
        
    } catch (error) {
        document.getElementById('battery-percent').textContent = '--';
        document.getElementById('battery-voltage').textContent = '--';
    }
}

// Render file list
function renderFileList(files) {
    const container = document.getElementById('file-list');
    
    if (!files || files.length === 0) {
        container.innerHTML = '<div class="text-muted text-center">No log files found</div>';
        return;
    }
    
    // Sort files by name (descending = newest first)
    files.sort((a, b) => b.name.localeCompare(a.name));
    
    container.innerHTML = files.map(file => {
        const isCsv = file.name.endsWith('.csv');
        const isBin = file.name.endsWith('.bin');
        const icon = isCsv ? '📄' : (isBin ? '💾' : '📁');
        
        return `
        <div class="file-item">
            <div>
                <div class="file-item__name">${icon} ${file.name}</div>
                <div class="file-item__meta">${UI.formatSize(file.size)}</div>
            </div>
            <div class="flex gap-sm">
                <button class="btn btn--small btn--primary" onclick="downloadFile('${file.name}')">⬇ Download</button>
                ${isBin ? `<button class="btn btn--small btn--secondary" onclick="convertFile('${file.name}')">🔄 Convert</button>` : ''}
            </div>
        </div>
    `}).join('');
}

export function refreshFiles() {
    updateFileList();
}

export function downloadFile(filename) {
    // Open download in new tab
    window.open('/api/download?file=' + encodeURIComponent(filename), '_blank');
}

export async function convertFile(filename) {
    const btn = event.target;
    const originalText = btn.textContent;
    btn.textContent = 'Converting...';
    btn.disabled = true;
    
    try {
        const data = await API.post('/api/convert?file=' + encodeURIComponent(filename), {});
        if (data.success) {
            UI.showAlert(`Converted: ${data.output_path.split('/').pop()}`, 'success');
            refreshFiles();
        } else {
            UI.showAlert('Conversion failed: ' + data.status, 'error');
        }
    } catch (error) {
        UI.showAlert('Conversion failed', 'error');
    } finally {
        btn.textContent = originalText;
        btn.disabled = false;
    }
}

// System status
async function updateSystem() {
    try {
        const status = await API.get('/api/status');
        
        // Uptime
        if (status.uptime_ms) {
            const sec = Math.floor(status.uptime_ms / 1000);
            const min = Math.floor(sec / 60);
            const hr = Math.floor(min / 60);
            document.getElementById('sys-uptime').textContent = `${hr}h ${min % 60}m ${sec % 60}s`;
        }
        
        // Heap
        if (status.free_heap) {
            document.getElementById('sys-heap').textContent = UI.formatSize(status.free_heap);
        }
        
        // Logging state
        isLogging = status.logging || false;
        updateLoggingButton();
        
    } catch (error) {
        // Silently fail
    }
}

// Start all updates
function startUpdates() {
    // Real-time data via SSE
    startStream();
    
    // Slower updates via polling
    setInterval(updateSDCard, 5000);
    setInterval(updateBattery, 10000);
    setInterval(updateSystem, 2000);
}

function stopUpdates() {
    stopStream();
}

// Initialize
function init() {
    initChart();
    startUpdates();
    
    // Initial data fetch
    updateSDCard();
    updateBattery();
    updateSystem();
    
    // Handle window resize
    window.addEventListener('resize', () => {
        initChart();
    });
}

window.addEventListener('beforeunload', stopUpdates);

// Expose functions globally for onclick handlers
window.toggleLogging = toggleLogging;
window.clearChart = clearChart;
window.setChartTimespan = setChartTimespan;
window.refreshFiles = refreshFiles;
window.downloadFile = downloadFile;
window.convertFile = convertFile;
window.hideSessionSummary = hideSessionSummary;

// Initialize on DOM ready
document.addEventListener('DOMContentLoaded', init);

