# Changelog

Все заметные изменения проекта. Формат — [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/),
версии — [Semantic Versioning](https://semver.org/lang/ru/).

## [Unreleased]

### Добавлено
- Экран почасового прогноза: три крупных столбца с шагом 3 часа (+3, +6, +9 ч),
  иконки 220×128, вероятность осадков.
- Демонстрационный фоновый режим для панелей (экспериментально).

### Изменено
- Сообщение об ошибке погоды на главном экране сокращено до «ошибка OWM» / «ошибка PW».

## [1.3.0] — 2026-10-24

### Добавлено
- HACS-интеграция: датчики прогноза на 5 дней (макс/мин температура, вероятность осадков).

## [1.2.1] — 2026-10-24

### Изменено
- Главный экран: короткое сообщение об ошибке погоды («ошибка OWM» / «ошибка PW»),
  подробности — в лог.

## [1.2.0] — 2026-10-24

### Добавлено
- HTTP-команды станции: `GET /api/cmd?c=screen|home|clock|reboot|brightness&v=NN`.
- Интеграция 1.2.0: кнопки «Сменить экран», «Экран Дома», «Большие часы», «Перезагрузка»
  и регулятор яркости.

## [1.1.2] — 2026-10-24

### Добавлено
- Файлы бренда интеграции по спецификации Home Assistant: `icon.png` 256×256,
  `icon@2x.png` 512×512, `logo.png` 512×256, `logo@2x.png` 1024×512.

## [1.1.1] — 2026-10-24

### Добавлено
- Логотип и иконка интеграции (папка `brand`).

## [1.1.0] — 2026-10-23

### Добавлено
- HTTP API станции `GET /api/state` (JSON: погода, датчики).
- Интеграция Home Assistant для HACS (опрос API, 12 датчиков + динамические Wi-Fi датчики).
- Скриншоты и заглавное изображение в README.

## [1.0.0] — 2026-10-22

### Добавлено
- Первый публичный релиз: прошивка станции и файловая система (иконки, луна, логотип).
- Экраны: главный, второй, «Дома», часы.
- Погодные источники: OpenWeatherMap, Pirate Weather.
- Датчики по Wi-Fi, MQTT Discovery, Telegram, будильник.

[Unreleased]: https://github.com/skycast-project/skycast/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/skycast-project/skycast/compare/v1.2.1...v1.3.0
[1.2.1]: https://github.com/skycast-project/skycast/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/skycast-project/skycast/compare/v1.1.2...v1.2.0
[1.1.2]: https://github.com/skycast-project/skycast/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/skycast-project/skycast/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/skycast-project/skycast/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/skycast-project/skycast/releases/tag/v1.0.0
