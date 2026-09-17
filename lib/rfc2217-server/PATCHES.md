# rfc2217-server — копия с патчем esp32-opto

Исходник: https://github.com/igrr/rfc2217-server, коммит
`281e424d4bca6c85fb8e4d727dd1fa0c315b95f1`, лицензия Apache-2.0
(`LICENSE.txt`). Скопированы `include/`, `src/`, `LICENSE.txt`;
`library.json` добавлен для PlatformIO.

Изменения (помечены в коде `esp32-opto patch`):

1. `on_client_connected` вызывается сразу после `accept()`, а не после
   согласования опции COM-PORT. Иначе простой TCP-клиент не прерывал бы
   опрос счётчика.
2. Добавлены колбэки `on_datasize`, `on_parity`, `on_stopsize`
   (`rfc2217_on_line_param_t`). Исходная библиотека только подтверждала эти
   команды клиенту и ничего не меняла.
