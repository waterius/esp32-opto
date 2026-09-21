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
    const submit = form.querySelector('button[type=submit]');
    if (submit) submit.disabled = true;
    ajax(action, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: data
    }, res => {
        if (submit) submit.disabled = false;
        if (res.errors && Object.keys(res.errors).length) {
            for (const k in res.errors) {
                const el = $(k + '-error');
                if (!el) continue;
                el.textContent = res.errors[k];
                el.classList.remove('hd');
            }
            // Форму не приняли — прошлый исход, если он висел, больше не верен
            form.querySelectorAll('p.ok, p.form-error').forEach(p => p.classList.add('hd'));
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

/* ---------- Статус ---------- */

function statusPage() {
    loadStatus();
    setInterval(loadStatus, 3000);
}

function loadStatus() {
    ajax('/api/status', {}, s => {
        let state = 'ок';
        if (s.meter_reading) state = 'идёт чтение…';
        else if (!s.meter_enabled) state = 'опрос выключен' + (s.meter_error ? ': ' + s.meter_error : '');
        else if (s.transparent) state = 'порт занят прозрачной сессией' +
            (s.transparent_idle_s > 60 ? ' (без обмена ' + Math.floor(s.transparent_idle_s / 60) + ' мин)' : '');
        else if (s.meter_error) state = s.meter_error;
        setText('meter-state', state);

        setText('serial', s.serial || '—');
        setText('model', s.model || '—');
        setText('meter-fw', s.meter_fw || '—');
        setText('meter-time', s.meter_time || '—');
        setText('read-at', s.has_reading ? fmtTime(s.read_at) : 'чтений не было');
        setText('total', s.has_reading ? fmtKwh(s.total) : '—');
        // Здесь показания счётчика, как он их отдал: T1…T4 и ничего больше.
        // Какой тариф дневной, а какой ночной, счётчик не знает — это выбор
        // человека, и он виден там, где делается: на странице настроек.
        const rows = $('tariffs');
        rows.innerHTML = '';
        s.tariffs.forEach((v, i) => {
            const tr = rows.insertRow();
            tr.insertCell().textContent = 'T' + (i + 1) + ', кВт·ч';
            tr.insertCell().textContent = fmtKwh(v);
        });

        let cloud = 'не отправляли';
        if (s.cloud_error) cloud = s.cloud_error + (s.cloud_code ? ' (HTTP ' + s.cloud_code + ')' : '');
        else if (s.cloud_code) cloud = 'HTTP ' + s.cloud_code + ', ' + fmtTime(s.cloud_at);
        setText('cloud', cloud);
        setText('cloud-next', Math.ceil(s.cloud_next_s / 60) + ' мин');

        setText('fw', s.fw);
        setText('ip', s.ip || '—');
        setText('rssi', s.rssi ? s.rssi + ' дБм' : '—');
        setText('uptime', fmtUptime(s.uptime_s));
        setText('heap', Math.round(s.heap / 1024) + ' КБ');
        setText('wifi-mode', s.wifi_mode);
        setText('boot-reason', s.boot_reason || '—');
        setText('wifi-drops', s.wifi_drops);
        $('safe-mode').classList.toggle('hd', !s.safe_mode);
        $('btn-read').disabled = !s.meter_enabled || s.safe_mode;
    });
}

function action(url, btn) {
    btn.disabled = true;
    post(url, () => {
        btn.disabled = false;
        loadStatus();
    });
}

function reboot(btn) {
    if (!confirm('Перезагрузить устройство?')) return;
    btn.disabled = true;
    post('/api/reboot', () => {});
}

/* ---------- Настройки ---------- */

function settingsPage() {
    ajax('/api/settings', {}, s => {
        const form = $('settings');
        for (const k in s) {
            const inp = form.elements[k];
            if (!inp) continue;
            if (inp.type == 'checkbox') inp.checked = !!s[k];
            else inp.value = s[k];
        }
    });
    loadReadingHints();
    // Настройки обычно открывают до первого опроса: без обновления там до
    // перезагрузки страницы висели бы прочерки вместо цифр, по которым и
    // выбирают код.
    setInterval(loadReadingHints, 5000);
}

// Показания над выбором кода: какой тариф дневной, а какой ночной, человек
// решает по цифрам — без них выбор вслепую, а цифры лежат на другой странице.
function loadReadingHints() {
    ajax('/api/status', {}, s => {
        setText('val-total', s.has_reading ? fmtKwh(s.total) : '—');
        for (let i = 0; i < 4; i++) {
            // Прочитано меньше тарифов, чем полей: остальные счётчик не отдал
            const read = s.has_reading && i < s.tariffs.length;
            setText('val-t' + (i + 1), read ? fmtKwh(s.tariffs[i]) : '—');
        }
    });
}

function saveSettings(event, form) {
    $('saved').classList.add('hd');
    formSubmit(event, form, '/api/settings', res => {
        showOk('saved', res.reboot ? 'Сохранено, устройство перезагружается…' : 'Сохранено');
    });
}

/* ---------- Wi-Fi ---------- */

// Сколько не доверять состоянию после отправки формы: прошивка применяет сеть
// в своём цикле, уже отдав ответ странице.
// Идёт попытка, начатая с этой страницы, и сеть, которую мы для неё отправили.
let wifiAttempt = false;
let wifiWantSsid = '';

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
        const state = WIFI_STATUS[s.status] || s.status;
        setText('wifi-status', state + (s.error ? ': ' + s.error : ''));
        setText('wifi-ssid', s.ssid || '—');
        setText('wifi-ip', s.ip || '—');
        setText('wifi-rssi', s.status == 'connected' ? s.rssi + ' дБм' : '—');
        setText('wifi-mode', s.mode);
        // Пока ответа на «Подключиться» нет, состояние дублируется под кнопкой:
        // таблица наверху страницы, и на телефоне до неё надо долистать
        if (wifiAttempt) showAttemptResult(s);
    });
}

function showAttemptResult(s) {
    // Состояние может быть ещё от прошлой сети: пока прошивка не применила
    // заявку, /api/wifi_status честно отвечает про старое подключение. Ждём,
    // пока устройство подтвердит, что взяло именно ту сеть, которую мы
    // отправили. По таймеру это не угадать: страницу обслуживает отдельная
    // задача и отвечает сразу, а loop() до заявки доходит через опрос счётчика
    // или запрос к облаку — это секунды.
    if (s.pending || s.ssid !== wifiWantSsid) return;
    if (s.status == 'connected') {
        wifiAttempt = false;
        setWifiResult('Подключено к сети ' + (s.ssid || '') + ', IP ' + s.ip +
            '. Дальше устройство доступно по этому адресу из домашней сети.', 'ok');
    } else if (s.status == 'failed' && s.error) {
        wifiAttempt = false;
        setWifiResult(s.error + '. Проверьте название сети и пароль и попробуйте снова.', 'form-error');
    }
}

function setWifiResult(text, cls) {
    const el = $('wifi-result');
    el.textContent = text;
    el.className = cls;  // ok — зелёным, form-error — красной плашкой
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
    // Сообщение показываем сразу, а не по ответу: устройство уходит на канал
    // роутера, телефон теряет его сеть, и ответ до страницы может не дойти
    setWifiResult('Подключаюсь к сети… Телефон может отключиться от устройства — это нормально.', 'ok');
    wifiWantSsid = $('ssid').value;
    formSubmit(event, form, '/api/wifi', () => {
        wifiAttempt = true;
        setWifiResult('Подключаюсь… Если связь с устройством пропала — снова подключитесь к его сети Wi-Fi.', 'ok');
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
