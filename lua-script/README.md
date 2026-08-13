# OBS Auto Capture Lua MVP

Это быстрый вариант рядом с C++-версией.

Важно: это не `.dll` плагин для папки `obs-plugins`. Это `Lua`-скрипт для OBS, который загружается через `Tools -> Scripts`.

## Что делает этот вариант

Скрипт отслеживает активное окно в Windows и по имени процесса включает нужный уже существующий source в OBS.

То есть логика такая:

1. Ты сам один раз создаешь в сцене обычные источники OBS:
   - `Window Capture`
   - `Game Capture`
2. Настраиваешь их вручную под нужные приложения.
3. Загружаешь этот Lua-скрипт.
4. В скрипте задаешь соответствие:
   - `notepad.exe => Notepad Capture`
   - `game.exe => Game Capture`
5. Когда активным становится окно нужного процесса, скрипт показывает соответствующий source и скрывает остальные из списка.

## Почему этот вариант быстрее

- Не нужен `CMake`
- Не нужен `Visual Studio`
- Не нужен SDK OBS для сборки
- Не нужно собирать `.dll`

## Установка

1. Открой OBS.
2. Перейди в `Tools -> Scripts`.
3. Нажми `+`.
4. Выбери файл [obs_auto_capture_switcher.lua](C:\programm\OBS%20auto-capture\lua-script\obs_auto_capture_switcher.lua).
5. В настройках скрипта укажи:
   - сцену, где лежат твои capture source
   - список соответствий `process.exe => Source Name`

## Пример настройки

Пусть в сцене уже созданы такие источники:

- `Game Capture Apex`
- `Discord Window`
- `Chrome Window`

Тогда в поле mappings можно вписать:

```text
r5apex.exe => Game Capture Apex
discord.exe => Discord Window
chrome.exe => Chrome Window
```

## Ограничения MVP

- Это не новый source типа "добавил как источник", а управляющий скрипт
- Источники `Window Capture` / `Game Capture` нужно создать заранее
- Переключение идет по активному foreground process
- Скрипт работает только на `Windows`

## Что удобно дальше

Если этот путь тебе подойдет, следующим шагом можно:

- добавить поддержку нескольких сцен
- сделать приоритеты
- переключать не только видимость, но и сам target у `Window Capture`
- потом вернуться к полноценному C++ source plugin
