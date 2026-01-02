/**
 * Factory Page - Hardware Testing and LED Control
 */

const testResults = {
    adc: null,
    imu: null,
    rtc: null,
    sd: null,
    neopixel: null,
    battery: null
};

// Log message to test log
function log(message, type = 'info') {
    const logDiv = document.getElementById('test-log');
    const timestamp = new Date().toLocaleTimeString();
    const colors = {
        info: 'var(--text-secondary)',
        success: 'var(--accent-green)',
        error: 'var(--accent-red)',
        warning: 'var(--accent-orange)'
    };

    const entry = document.createElement('div');
    entry.style.color = colors[type] || colors.info;
    entry.textContent = `[${timestamp}] ${message}`;

    // Remove placeholder if present
    const placeholder = logDiv.querySelector('.text-muted');
    if (placeholder) placeholder.remove();

    logDiv.appendChild(entry);
    logDiv.scrollTop = logDiv.scrollHeight;
}

export function clearLog() {
    document.getElementById('test-log').innerHTML =
        '<div class="text-muted">Test log cleared...</div>';
}

// Run individual test
export async function runTest(sensor) {
    log(`Starting ${sensor.toUpperCase()} test...`);

    const testDiv = document.getElementById(`test-${sensor}`);
    const statusSpan = document.getElementById(`test-${sensor}-status`);
    const detailsDiv = document.getElementById(`test-${sensor}-details`);

    statusSpan.textContent = 'Testing...';
    testDiv.className = 'test-result test-result--pending';

    try {
        const result = await API.post(`/api/test/${sensor}`, {});
        testResults[sensor] = result;

        if (result.passed) {
            log(`${sensor.toUpperCase()} test PASSED: ${result.message}`, 'success');
            statusSpan.textContent = '✓ Pass';
            testDiv.className = 'test-result test-result--pass';
        } else {
            log(`${sensor.toUpperCase()} test FAILED: ${result.message}`, 'error');
            statusSpan.textContent = '✗ Fail';
            testDiv.className = 'test-result test-result--fail';
        }

        // Show details
        if (result.details) {
            const detailStr = Object.entries(result.details)
                .map(([k, v]) => `${k}: ${JSON.stringify(v)}`)
                .join(', ');
            detailsDiv.textContent = detailStr;
        }

    } catch (error) {
        log(`${sensor.toUpperCase()} test ERROR: ${error.message}`, 'error');
        statusSpan.textContent = '✗ Error';
        testDiv.className = 'test-result test-result--fail';
        testResults[sensor] = { passed: false, message: error.message };
    }

    updateSummary();
}

// Run all tests sequentially
export async function runAllTests() {
    log('=== Starting All Tests ===', 'info');
    resetTests();

    const sensors = ['adc', 'imu', 'rtc', 'sd', 'neopixel', 'battery'];

    for (const sensor of sensors) {
        await runTest(sensor);
        await new Promise(r => setTimeout(r, 500)); // Brief pause between tests
    }

    log('=== All Tests Complete ===', 'info');

    const summary = calculateSummary();
    if (summary.fail === 0 && summary.pass === summary.total) {
        log('OVERALL RESULT: ALL TESTS PASSED', 'success');
    } else {
        log(`OVERALL RESULT: ${summary.fail} TEST(S) FAILED`, 'error');
    }
}

// Reset all tests
export function resetTests() {
    const sensors = ['adc', 'imu', 'rtc', 'sd', 'neopixel', 'battery'];

    sensors.forEach(sensor => {
        testResults[sensor] = null;
        const testDiv = document.getElementById(`test-${sensor}`);
        const statusSpan = document.getElementById(`test-${sensor}-status`);

        testDiv.className = 'test-result test-result--pending';
        statusSpan.textContent = 'Pending';
    });

    updateSummary();
    log('Tests reset', 'info');
}

// Calculate test summary
function calculateSummary() {
    const sensors = Object.keys(testResults);
    let pass = 0, fail = 0, pending = 0;

    sensors.forEach(sensor => {
        if (testResults[sensor] === null) {
            pending++;
        } else if (testResults[sensor].passed) {
            pass++;
        } else {
            fail++;
        }
    });

    return { total: sensors.length, pass, fail, pending };
}

// Update summary display
function updateSummary() {
    const summary = calculateSummary();

    document.getElementById('summary-total').textContent = summary.total;
    document.getElementById('summary-pass').textContent = summary.pass;
    document.getElementById('summary-fail').textContent = summary.fail;
    document.getElementById('summary-pending').textContent = summary.pending;

    const resultEl = document.getElementById('summary-result');
    if (summary.pending === summary.total) {
        resultEl.textContent = '--';
        resultEl.style.color = 'var(--text-muted)';
    } else if (summary.fail > 0) {
        resultEl.textContent = 'FAIL';
        resultEl.style.color = 'var(--accent-red)';
    } else if (summary.pending === 0) {
        resultEl.textContent = 'PASS';
        resultEl.style.color = 'var(--accent-green)';
    } else {
        resultEl.textContent = '...';
        resultEl.style.color = 'var(--accent-orange)';
    }
}

// ========================================================================
// LED Test Functions
// ========================================================================

let ledCycling = false;
let selectedColor = 'red';
let selectedPattern = 'steady';

// Fetch and update LED state display
async function refreshLedState() {
    try {
        const state = await API.get('/api/led');
        document.getElementById('led-state-name').textContent = state.state_name || '--';
        document.getElementById('led-state-progress').textContent =
            `${(state.state_index || 0) + 1} / ${state.state_count || 0}`;

        ledCycling = state.cycling || false;
        updateCycleButton();
    } catch (error) {
        console.error('Failed to fetch LED state:', error);
    }
}

// Update cycle button appearance
function updateCycleButton() {
    const btn = document.getElementById('btn-cycle-toggle');
    const indicator = document.getElementById('led-cycle-indicator');

    if (ledCycling) {
        btn.textContent = '⏹ Stop Auto Cycle';
        btn.className = 'btn btn--secondary';
        indicator.style.display = 'block';
    } else {
        btn.textContent = '▶ Start Auto Cycle';
        btn.className = 'btn btn--primary';
        indicator.style.display = 'none';
    }
}

// Toggle auto-cycle
export async function toggleAutoCycle() {
    try {
        if (ledCycling) {
            await API.post('/api/led/cycle/stop', {});
            log('LED auto-cycle stopped');
        } else {
            await API.post('/api/led/cycle/start', { interval_ms: 1500 });
            log('LED auto-cycle started (1.5s interval)');
        }
        await refreshLedState();
    } catch (error) {
        log(`Failed to toggle cycle: ${error.message}`, 'error');
    }
}

// Advance to next LED state
export async function ledNext() {
    try {
        const result = await API.post('/api/led/next', {});
        log(`LED: ${result.state_name}`);
        await refreshLedState();
    } catch (error) {
        log(`Failed to advance LED: ${error.message}`, 'error');
    }
}

// Set LED color with current pattern
export async function setLed(color) {
    selectedColor = color;
    log(`Setting LED: ${color} (${selectedPattern})`);
    try {
        await API.post('/api/led', {
            color: color,
            pattern: selectedPattern
        });
        await refreshLedState();
    } catch (error) {
        log(`Failed to set LED: ${error.message}`, 'error');
    }
}

// Set pattern and apply to current color
export async function setPattern(pattern) {
    selectedPattern = pattern;

    // Update button styles
    document.querySelectorAll('.pattern-btn').forEach(btn => {
        btn.classList.remove('btn--primary');
        btn.classList.add('btn--secondary');
    });
    const activeBtn = document.querySelector(`[data-pattern="${pattern}"]`);
    if (activeBtn) {
        activeBtn.classList.remove('btn--secondary');
        activeBtn.classList.add('btn--primary');
    }

    log(`Pattern: ${pattern}`);
    try {
        await API.post('/api/led', {
            color: selectedColor,
            pattern: pattern
        });
        await refreshLedState();
    } catch (error) {
        log(`Failed to set pattern: ${error.message}`, 'error');
    }
}

// Set error blink code
export async function setBlinkCode(count) {
    log(`Setting error blink code: ${count} blinks`);
    try {
        await API.post('/api/led', {
            color: 'red',
            pattern: 'blink_code',
            blink_count: count
        });
        await refreshLedState();
    } catch (error) {
        log(`Failed to set blink code: ${error.message}`, 'error');
    }
}

// Exit factory mode
export async function exitFactoryMode() {
    // Stop auto-cycling before exit
    if (ledCycling) {
        await API.post('/api/led/cycle/stop', {});
    }

    const success = await Mode.switchTo('user');
    if (success) {
        location.href = 'index.html';
    }
}

// Initialize
function init() {
    log('Factory Test Mode initialized');
    updateSummary();

    // Verify we're in factory mode
    Mode.fetch().then(mode => {
        if (mode !== 'factory') {
            UI.showAlert('Not in Factory mode. Please switch modes from the main page.', 'warning');
        }
    });

    // Fetch initial LED state
    refreshLedState();

    // Periodically refresh LED state when cycling
    setInterval(() => {
        if (ledCycling) {
            refreshLedState();
        }
    }, 500);
}

// Expose functions globally for onclick handlers
window.runTest = runTest;
window.runAllTests = runAllTests;
window.resetTests = resetTests;
window.clearLog = clearLog;
window.toggleAutoCycle = toggleAutoCycle;
window.ledNext = ledNext;
window.setLed = setLed;
window.setPattern = setPattern;
window.setBlinkCode = setBlinkCode;
window.exitFactoryMode = exitFactoryMode;

// Initialize on DOM ready
document.addEventListener('DOMContentLoaded', init);

