// Юнит-тесты протокола обмена со счётчиком НАРТИС (СПОДЭС = DLMS/COSEM поверх HDLC).
//
// Адаптер проверяется снаружи, через байты оптопорта: на реальном дампе
// (docs/05-nartis-100-exchange.md) и на эмуляторе счётчика. Поэтому тесты
// пережили замену своего клиента DLMS на Gurux: поменялся только readMeter(),
// и при следующей смене реализации будет так же.
//
//   ~/.platformio/penv/bin/pio test -e native
#include <unity.h>

#include <cstring>

#include "core/nartis.h"
#include "core_clock.h"
#include "meter_emulator.h"

// Результат одного опроса. Единственное место, где тесты вызывают адаптер.
struct Reading {
    bool ok = false;
    core::ReadResult result = core::ReadResult::Failed;
    core::MeterData data;
    char error[core::METER_ERROR_CAP] = {0};
    uint8_t addr = 0;
};

static Reading readMeter(core::IOptoPort& port, uint8_t addr = 0, const char* pwd = "111") {
    core::NartisMeter meter(port);
    meter.setAddress(addr);
    meter.setPassword(pwd);
    Reading r;
    r.result = meter.read(r.data, r.error, sizeof(r.error));
    r.ok = r.result == core::ReadResult::Ok;
    r.addr = meter.foundAddress();
    return r;
}

void setUp() { testclock::reset(); }
void tearDown() {}

// Строка UTF-8 не оборвана посреди символа
static bool utf8Complete(const char* s) {
    size_t n = strlen(s), i = 0;
    while (i < n) {
        uint8_t c = (uint8_t)s[i];
        size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        if (i + len > n) return false;
        i += len;
    }
    return true;
}

// --- Реальный счётчик ------------------------------------------------------

void test_real_dump_requests_are_byte_identical() {
    RealExchangePort port;
    readMeter(port);
    for (const std::string& req : port.unmatched) TEST_MESSAGE(req.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(0, port.unmatched.size(), "запросы, которых не было в реальном обмене");
}

void test_real_dump_values() {
    RealExchangePort port;
    Reading r = readMeter(port);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
    TEST_ASSERT_EQUAL_UINT8(16, r.addr);
    TEST_ASSERT_EQUAL_DOUBLE(1661.974, r.data.total);
    TEST_ASSERT_EQUAL_UINT8(4, r.data.tariffCount);
    TEST_ASSERT_EQUAL_DOUBLE(1150.614, r.data.tariff[0]);
    TEST_ASSERT_EQUAL_DOUBLE(511.360, r.data.tariff[1]);
    TEST_ASSERT_EQUAL_DOUBLE(0, r.data.tariff[2]);
    TEST_ASSERT_EQUAL_DOUBLE(0, r.data.tariff[3]);
    TEST_ASSERT_EQUAL_STRING("52207839", r.data.serial);
    TEST_ASSERT_EQUAL_STRING("НАРТИС-100.121RL", r.data.model);
    TEST_ASSERT_EQUAL_STRING("255.06", r.data.fwVersion);
    TEST_ASSERT_EQUAL_STRING("2026-09-17 12:15:06", r.data.time);
}

// --- Кадры HDLC ------------------------------------------------------------

void test_frames_are_valid_and_addressed() {
    MeterEmulator meter;
    readMeter(meter);
    TEST_ASSERT_EQUAL(0, meter.badFrames);
    for (const Bytes& raw : meter.frames) {
        Frame f = parseFrame(raw);
        TEST_ASSERT_TRUE(f.ok);
        TEST_ASSERT_EQUAL_HEX8(0x02, f.dst[0]);  // logical 1
        TEST_ASSERT_EQUAL_HEX8(0x21, f.dst[1]);  // physical 16
        TEST_ASSERT_EQUAL_HEX8(0x41, f.src[0]);  // клиент 32
    }
}

void test_session_order_snrm_aarq_disc() {
    MeterEmulator meter;
    readMeter(meter);
    TEST_ASSERT_EQUAL_HEX8(0x93, parseFrame(meter.frames.front()).control);  // SNRM
    Frame aarq = meter.iFrames().front();
    TEST_ASSERT_EQUAL_HEX8(0x60, aarq.info[3]);
    TEST_ASSERT_EQUAL_HEX8(0x53, parseFrame(meter.frames.back()).control);  // DISC
    TEST_ASSERT_EQUAL(1, meter.discs);
}

void test_sequence_numbers_wrap_modulo_8() {
    MeterEmulator meter;
    readMeter(meter);
    std::vector<Frame> req = meter.iFrames();
    TEST_ASSERT_GREATER_THAN(8, req.size());  // счётчики успевают пройти через 7 → 0
    for (size_t i = 0; i < req.size(); i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(i % 8, (req[i].control >> 1) & 7, "N(S)");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(i % 8, req[i].control >> 5, "N(R)");
        TEST_ASSERT_TRUE_MESSAGE(req[i].control & 0x10, "бит P");
    }
}

void test_segmented_response_is_reassembled() {
    MeterEmulator meter;
    meter.maxInfo = 8;  // ответы режутся на кадры по 8 байт
    Reading r = readMeter(meter);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
    TEST_ASSERT_GREATER_THAN(0, meter.rrs);
    TEST_ASSERT_EQUAL_STRING("НАРТИС-100.121RL", r.data.model);
    TEST_ASSERT_EQUAL_STRING("2026-09-17 12:15:06", r.data.time);
    TEST_ASSERT_EQUAL_DOUBLE(1661.974, r.data.total);
}

void test_corrupted_fcs_is_not_accepted() {
    MeterEmulator meter;
    meter.corruptFrames = 1;  // испорчен UA на адрес 16
    Reading r = readMeter(meter, 16);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL(0, meter.aarqs);
}

// --- Арбитраж шины ----------------------------------------------------------

// Прозрачная сессия RFC 2217 забрала порт посреди опроса (после суммы и
// первого тарифа): чтение обязано остановиться сразу, без дополнительных
// кадров на строки и время — иначе они уходят в оптопорт мимо арбитра.
void test_abort_stops_further_reads() {
    MeterEmulator meter;
    meter.abortAfterGets = 4;  // сумма (2 GET) + T1 (2 GET) — дальше порт забран
    Reading r = readMeter(meter);
    TEST_ASSERT_TRUE(r.result == core::ReadResult::Aborted);
    TEST_ASSERT_EQUAL(4, meter.gets);            // ни одного лишнего запроса после прерывания
    TEST_ASSERT_EQUAL(6, meter.frames.size());   // SNRM + AARQ + 4 GET, без DISC
    TEST_ASSERT_EQUAL(0, meter.discs);           // порт уже не у нас — DISC не шлём
}

// Прерывание пришло не до группы readString/readClock, а внутри неё — во
// время чтения серийного номера (первый вызов группы). Оставшиеся три вызова
// (модель, версия ПО, время) не должны отправить ни кадра: их останавливает
// проверка в Session::sendAndReceive, а не групповой if (!s.aborted()), у
// которого нет шанса сработать между вызовами внутри уже открытого блока.
void test_abort_inside_group_stops_remaining_calls() {
    MeterEmulator meter;
    meter.abortAfterGets = 11;  // сумма + T1..T4 (10 GET) + серийный номер (11-й)
    Reading r = readMeter(meter);
    TEST_ASSERT_TRUE(r.result == core::ReadResult::Aborted);
    TEST_ASSERT_EQUAL(11, meter.gets);           // модель/ПО/время запросов не отправляли
    TEST_ASSERT_EQUAL(13, meter.frames.size());  // SNRM+AARQ+10 GET(энергия)+1 GET(серийный)
    TEST_ASSERT_EQUAL(0, meter.discs);
    TEST_ASSERT_EQUAL_STRING("", r.data.serial);  // ответ был готов, но принят не был
}

// --- Адрес и пароль --------------------------------------------------------

void test_address_probe_16_then_17() {
    MeterEmulator meter;
    meter.phys = 17;  // серия И
    Reading r = readMeter(meter);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
    TEST_ASSERT_EQUAL_UINT8(17, r.addr);
    TEST_ASSERT_EQUAL(2, meter.snrmAddrs.size());
    TEST_ASSERT_EQUAL_UINT8(16, meter.snrmAddrs[0]);
    TEST_ASSERT_EQUAL_UINT8(17, meter.snrmAddrs[1]);
    TEST_ASSERT_EQUAL(1, meter.aarqs);
}

void test_fixed_address_is_not_probed() {
    MeterEmulator meter;
    meter.phys = 17;
    Reading r = readMeter(meter, 16);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL(1, meter.snrmAddrs.size());
    TEST_ASSERT_EQUAL_UINT8(16, meter.snrmAddrs[0]);
}

void test_password_goes_into_aarq() {
    MeterEmulator meter;
    meter.password = "12345678";
    Reading r = readMeter(meter, 0, "12345678");
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
    TEST_ASSERT_EQUAL(1, meter.aarqs);
}

void test_wrong_password_is_sent_once() {
    MeterEmulator meter;
    meter.password = "12345";
    Reading r = readMeter(meter);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL(1, meter.aarqs);
    TEST_ASSERT_EQUAL(0, meter.gets);
    TEST_ASSERT_TRUE(r.result == core::ReadResult::AuthRejected);
    TEST_ASSERT_TRUE(strlen(r.error) > 0);
}

// Пять неверных паролей блокируют счётчик на сутки. Даже если на SNRM
// ответили оба адреса, после отказа в пароле второй попытки быть не должно.
void test_wrong_password_is_not_retried_on_other_address() {
    MeterEmulator meter;
    meter.anyAddress = true;
    meter.password = "12345";
    Reading r = readMeter(meter);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(r.result == core::ReadResult::AuthRejected);
    TEST_ASSERT_EQUAL(1, meter.aarqs);
}

void test_silent_meter() {
    MeterEmulator meter;
    meter.silent = true;
    Reading r = readMeter(meter);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL(0, meter.aarqs);
    TEST_ASSERT_EQUAL(2, meter.snrmAddrs.size());
    TEST_ASSERT_TRUE(strlen(r.error) > 0);
}

// Текст ошибки идёт прямо на страницу статуса. Кириллица в UTF-8 — два байта
// на букву, и сообщение с адресом и кодом в 64 байта не влезало: на странице
// была «нет связи со счётчиком (адрес 16, ко» с разрубленной пополам буквой.
void test_error_message_is_not_cut() {
    MeterEmulator meter;
    meter.silent = true;
    Reading r = readMeter(meter);

    TEST_ASSERT_TRUE_MESSAGE(utf8Complete(r.error), r.error);
    TEST_ASSERT_TRUE_MESSAGE(strlen(r.error) > 0, "сообщения нет вовсе");
    // Сообщение заканчивается кодом в скобках: оборванный текст скобку потеряет
    TEST_ASSERT_EQUAL_MESSAGE(')', r.error[strlen(r.error) - 1], r.error);
}

void test_auth_rejected_message_is_not_cut() {
    MeterEmulator meter;
    meter.anyAddress = true;
    meter.password = "12345";  // прошивка пойдёт с заводским 111 — счётчик откажет
    Reading r = readMeter(meter);

    TEST_ASSERT_TRUE(r.result == core::ReadResult::AuthRejected);
    TEST_ASSERT_TRUE_MESSAGE(utf8Complete(r.error), r.error);
    TEST_ASSERT_EQUAL_MESSAGE(')', r.error[strlen(r.error) - 1], r.error);
}

// --- Данные ----------------------------------------------------------------

void test_tariffs_read_until_first_error() {
    MeterEmulator meter;
    meter.remove(3, "1.0.1.8.3.255", 2);
    Reading r = readMeter(meter);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
    TEST_ASSERT_EQUAL_UINT8(2, r.data.tariffCount);
    TEST_ASSERT_EQUAL_DOUBLE(1150.614, r.data.tariff[0]);
    TEST_ASSERT_EQUAL_DOUBLE(511.360, r.data.tariff[1]);
}

void test_total_energy_is_required() {
    MeterEmulator meter;
    meter.remove(3, "1.0.1.8.0.255", 2);
    Reading r = readMeter(meter);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL(1, meter.discs);  // связь всё равно закрыта
}

void test_energy_scaler_and_unit() {
    MeterEmulator meter;
    meter.set(3, "1.0.1.8.0.255", 3, "02 02 0f ff 16 1e");  // scaler −1, Вт·ч
    meter.set(3, "1.0.1.8.1.255", 3, "02 02 0f 02 16 1e");  // scaler +2, Вт·ч
    meter.set(3, "1.0.1.8.2.255", 2, "15 00 00 00 00 00 07 cd 80");  // long64-unsigned
    Reading r = readMeter(meter);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
    TEST_ASSERT_EQUAL_DOUBLE(166.1974, r.data.total);
    TEST_ASSERT_EQUAL_DOUBLE(115061.4, r.data.tariff[0]);
    TEST_ASSERT_EQUAL_DOUBLE(511.360, r.data.tariff[1]);
}

void test_strings_cp1251_to_utf8() {
    MeterEmulator meter;
    // "Ёж ёлка" в cp1251
    meter.set(1, "0.0.96.1.1.255", 2, "09 07 a8 e6 20 b8 eb ea e0");
    Reading r = readMeter(meter);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
    TEST_ASSERT_EQUAL_STRING("Ёж ёлка", r.data.model);
}

void test_long_string_is_cut_on_character_boundary() {
    MeterEmulator meter;
    // 20 букв «Я»: в UTF-8 40 байт, в model не влезает
    meter.set(1, "0.0.96.1.1.255", 2,
              "09 14 df df df df df df df df df df df df df df df df df df df df");
    Reading r = readMeter(meter);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
    TEST_ASSERT_TRUE(strlen(r.data.model) > 0);
    TEST_ASSERT_TRUE(strlen(r.data.model) < sizeof(r.data.model));
    TEST_ASSERT_TRUE(utf8Complete(r.data.model));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_real_dump_requests_are_byte_identical);
    RUN_TEST(test_real_dump_values);
    RUN_TEST(test_frames_are_valid_and_addressed);
    RUN_TEST(test_session_order_snrm_aarq_disc);
    RUN_TEST(test_sequence_numbers_wrap_modulo_8);
    RUN_TEST(test_segmented_response_is_reassembled);
    RUN_TEST(test_corrupted_fcs_is_not_accepted);
    RUN_TEST(test_abort_stops_further_reads);
    RUN_TEST(test_abort_inside_group_stops_remaining_calls);
    RUN_TEST(test_address_probe_16_then_17);
    RUN_TEST(test_fixed_address_is_not_probed);
    RUN_TEST(test_password_goes_into_aarq);
    RUN_TEST(test_wrong_password_is_sent_once);
    RUN_TEST(test_wrong_password_is_not_retried_on_other_address);
    RUN_TEST(test_silent_meter);
    RUN_TEST(test_error_message_is_not_cut);
    RUN_TEST(test_auth_rejected_message_is_not_cut);
    RUN_TEST(test_tariffs_read_until_first_error);
    RUN_TEST(test_total_energy_is_required);
    RUN_TEST(test_energy_scaler_and_unit);
    RUN_TEST(test_strings_cp1251_to_utf8);
    RUN_TEST(test_long_string_is_cut_on_character_boundary);
    return UNITY_END();
}
