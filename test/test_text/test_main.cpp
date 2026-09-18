// Юнит-тесты обрезки UTF-8 (src/core/text.h): лог отдаётся странице кусками по
// 4 КБ, и граница куска не должна рассекать кириллическую букву пополам.
//
//   ~/.platformio/penv/bin/pio test -e native -f test_text
#include <unity.h>

#include <string.h>

#include "core/text.h"

using core::utf8Trim;

void setUp() {}
void tearDown() {}

void test_ascii_is_never_trimmed() {
    const char* s = "RX 7e a0 1f";
    TEST_ASSERT_EQUAL(strlen(s), utf8Trim(s, strlen(s)));
}

void test_empty_text() { TEST_ASSERT_EQUAL(0, utf8Trim("", 0)); }

void test_whole_two_byte_char_is_kept() {
    const char* s = "Оптопорт";  // «О» — два байта
    TEST_ASSERT_EQUAL(strlen(s), utf8Trim(s, strlen(s)));
}

void test_cut_two_byte_char_is_dropped() {
    const char* s = "Оптопорт";
    // Режем ровно посередине первой буквы
    TEST_ASSERT_EQUAL_MESSAGE(0, utf8Trim(s, 1), "половинка буквы осталась в куске");
}

void test_cut_in_the_middle_of_a_word() {
    const char* s = "Счётчик";
    size_t full = strlen(s);
    for (size_t cut = 0; cut <= full; ++cut) {
        size_t kept = utf8Trim(s, cut);
        TEST_ASSERT_TRUE_MESSAGE(kept <= cut, "обрезка удлинила кусок");
        // Проверяем, что оставшееся — целые символы
        size_t i = 0;
        while (i < kept) {
            uint8_t c = (uint8_t)s[i];
            size_t need = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
            TEST_ASSERT_TRUE_MESSAGE(i + need <= kept, "в куске остался оборванный символ");
            i += need;
        }
        TEST_ASSERT_EQUAL_MESSAGE(kept, i, "разбор не сошёлся с длиной");
    }
}

void test_three_and_four_byte_chars() {
    const char* s = "\xE2\x82\xAC\xF0\x9F\x94\x8C";  // € и розетка
    TEST_ASSERT_EQUAL(7, utf8Trim(s, 7));
    TEST_ASSERT_EQUAL(3, utf8Trim(s, 6));  // четырёхбайтовый оборван
    TEST_ASSERT_EQUAL(3, utf8Trim(s, 4));
    TEST_ASSERT_EQUAL(0, utf8Trim(s, 2));  // трёхбайтовый оборван
}

void test_progress_is_always_made() {
    // Кусок, начинающийся с байтов-продолжений (начало символа уже уехало
    // в прошлый кусок): обрезка не должна возвращать ноль вечно
    const char* s = "\x82\xAC ok";
    TEST_ASSERT_EQUAL(strlen(s), utf8Trim(s, strlen(s)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ascii_is_never_trimmed);
    RUN_TEST(test_empty_text);
    RUN_TEST(test_whole_two_byte_char_is_kept);
    RUN_TEST(test_cut_two_byte_char_is_dropped);
    RUN_TEST(test_cut_in_the_middle_of_a_word);
    RUN_TEST(test_three_and_four_byte_chars);
    RUN_TEST(test_progress_is_always_made);
    return UNITY_END();
}
