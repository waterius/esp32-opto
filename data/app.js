// Общий код четырёх страниц. Запросы с повторами, отправка форм и список сетей —
// по мотивам портала waterius (ESP8266/data/static/common.js).

const AJAX_TRIES = 3;
const AJAX_RETRY_MS = 1000;

function $(id) { return document.getElementById(id); }

function ajax(url, opts, callback, _try = 0) {
    fetch(url, opts)
        .then(res => res.ok ? res.text() : Promise.reject(res))
        .then(text => {
            linkLost(false);
            let data = text;
            try { data = JSON.parse(text); } catch (e) {}
            callback(data);
        })
        .catch(err => {
            if (++_try < AJAX_TRIES) {
                setTimeout(() => ajax(url, opts, callback, _try), AJAX_RETRY_MS);
                return;
            }
            // Ответ с кодом — ошибка прошивки; ответа нет вовсе — устройство недоступно
            if (err && err.status !== undefined) alert('Ошибка ' + err.status);
            else linkLost(true);
        });
}

function linkLost(lost) {
    const box = $('link-lost');
    if (box) box.classList.toggle('hd', !lost);
}

function post(url, callback) {
    ajax(url, { method: 'POST' }, callback);
}

// Как formSubmit в waterius: чекбокс уходит как 1/0, ошибки полей — в <p id="имя-error">
function formSubmit(event, form, action, done) {
    event.preventDefault();
    const data = new URLSearchParams();
    form.querySelectorAll('input,select').forEach(inp => {
        if (!inp.name) return;
        if (inp.type == 'checkbox') return data.append(inp.name, inp.checked ? 1 : 0);
        data.append(inp.name, inp.value.trim());
    });
    form.querySelectorAll('p.error').forEach(p => p.classList.add('hd'));
    ajax(action, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: data
    }, res => {
        if (res.errors && Object.keys(res.errors).length) {
            for (const k in res.errors) {
                const el = $(k + '-error');
                if (!el) continue;
                el.textContent = res.errors[k];
                el.classList.remove('hd');
            }
            return;
        }
        if (done) done(res);
    });
}

function showPW(id) {
    const pw = $(id);
    pw.type = pw.type == 'password' ? 'text' : 'password';
}

function setText(id, text) {
    const el = $(id);
    if (el) el.textContent = text;
}

function showOk(id, text) {
    setText(id, text);
    $(id).classList.remove('hd');
}

function fmtTime(epoch) {
    return epoch ? new Date(epoch * 1000).toLocaleString('ru-RU') : 'время неизвестно';
}

function fmtKwh(v) { return Number(v).toFixed(3); }

function fmtUptime(s) {
    const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
    return (d ? d + ' д ' : '') + h + ' ч ' + m + ' мин';
}

/* ---------- Wi-Fi ---------- */

const WIFI_STATUS = {
    idle: 'не подключено',
    connecting: 'подключение…',
    connected: 'подключено',
    failed: 'не удалось подключиться'
};

function wifiPage() {
    $('ssid').addEventListener('input', () => {
        // Имя введено вручную — канала и BSSID нет, будет полный скан (как в waterius)
        $('wifi_channel').value = '';
        $('bssid').value = '';
    });
    loadNetworks();
    loadWifiStatus();
    setInterval(loadWifiStatus, 3000);
}

function loadWifiStatus() {
    ajax('/api/wifi_status', {}, s => {
        setText('wifi-status', WIFI_STATUS[s.status] || s.status);
        setText('wifi-ssid', s.ssid || '—');
        setText('wifi-ip', s.ip || '—');
        setText('wifi-rssi', s.status == 'connected' ? s.rssi + ' дБм' : '—');
        setText('wifi-mode', s.mode);
    });
}

function loadNetworks() {
    $('networks').innerHTML = '<p class="text">Поиск сетей…</p>';
    ajax('/api/networks', {}, data => {
        if (data.scanning) return setTimeout(loadNetworks, 1500);
        renderNetworks(data);
    });
}

function renderNetworks(list) {
    const box = $('networks');
    box.innerHTML = '';
    if (!list.length) {
        box.innerHTML = '<p class="text">Сети не найдены</p>';
        return;
    }
    list.sort((a, b) => b.level - a.level).forEach(n => {
        // Имя сети — через textContent: оно приходит из эфира
        const row = document.createElement('div');
        row.className = 'net';
        const name = document.createElement('span');
        name.textContent = n.ssid || '(скрытая сеть)';
        const level = document.createElement('span');
        level.className = 'lvl';
        level.textContent = '▮'.repeat(n.level) + '▯'.repeat(4 - n.level) + (n.open ? ' открытая' : '');
        row.appendChild(name);
        row.appendChild(level);
        row.onclick = () => {
            $('ssid').value = n.ssid;
            $('wifi_channel').value = n.wifi_channel;
            $('bssid').value = n.bssid;
            $('password').focus();
        };
        box.appendChild(row);
    });
}

function saveWifi(event, form) {
    $('wifi-result').classList.add('hd');
    formSubmit(event, form, '/api/wifi', () => {
        showOk('wifi-result', 'Подключаюсь… Если связь с устройством пропала — снова подключитесь к его сети Wi-Fi.');
    });
}

/* ---------- Лог ---------- */

const LOG_POLL_MS = 1000;
const LOG_MAX_CHARS = 200000;  // дальше старый текст на экране обрезается

let logFrom = 0;
let logBoot = null;

function logPage() {
    loadLog();
}

// Свой цикл вместо ajax(): при обрыве связи опрос не должен останавливаться
function loadLog() {
    fetch('/api/log?from=' + logFrom)
        .then(res => res.ok ? res.json() : Promise.reject(res))
        .then(d => {
            linkLost(false);
            if (logBoot !== null && d.boot !== logBoot) {
                // Устройство перезагрузилось: позиция from относилась к прошлой загрузке
                logBoot = d.boot;
                logFrom = 0;
                logAppend('\n--- устройство перезагрузилось ---\n');
                return;
            }
            logBoot = d.boot;
            if (d.skipped) logAppend('\n--- часть лога пропущена: буфер переполнился ---\n');
            logAppend(d.text);
            logFrom = d.next;
        })
        .catch(() => linkLost(true))
        .finally(() => setTimeout(loadLog, LOG_POLL_MS));
}

function logAppend(text) {
    if (!text) return;
    const box = $('log');
    box.textContent = (box.textContent + text.replace(/\r/g, '')).slice(-LOG_MAX_CHARS);
    if ($('log-follow').checked) box.scrollTop = box.scrollHeight;
}
