# catingiran (`cti`) v1.0.0

Быстрая терминальная IDE и редактор кода на чистом C11 в стиле Vim / OnVim под лицензией **BSD-3-Clause** без внешних библиотек сборки.

## Основные возможности

* **Vim-движок мирового уровня**:
  * Счётчики команд (`5dd`, `10j`, `3w`, `4x`) и операторы с текстовыми объектами (`ciw`, `diw`, `yiw`, `di"`, `da"`, `di(`, `da(`, `di{`, `da{`, `vi(`).
  * Запись и воспроизведение макросов (`qa` ... `q`, `@a`, `2@a`), `Undo` (`u`) и `Redo` (`Ctrl+R` / `:redo`).
  * Поиск и замена во всём файле `:%s/старое/новое/g` и в текущей строке `:s/старое/новое/g` с поддержкой `ignore_case` и `smart_case`.
* **Настоящий асинхронный LSP-клиент (JSON-RPC 2.0)**:
  * Автозапуск `clangd` (C/C++), `pyright-langserver` / `pylsp` (Python) и `jdtls` (Java).
  * Живая диагностика ошибок прямо в редакторе (`●`/`▲`), переход к определению (`gd`), поиск ссылок (`gr`), всплывающая документация (`K`), переименование символа (`Space + rn`) и `:lsp`.
* **Умный Live-автокомплит**: всплывающее меню дополнения символов от LSP, плагинов, стандартных сниппетов и буфера (`Tab` / `S-Tab` / `Enter` / `Ctrl+Space`).
* **Проводник файлов как в VS Code**: левая или правая боковая панель (`Ctrl+B` / `F2` / `:ot` / `:ht`) с иконками Nerd Font, мышью и созданием/удалением/переименованием файлов.
* **IDE-инструменты**:
  * Встроенный компилятор и запуск (`F5` / `:run` / `Space+rr`) с нижней панелью вывода (`Ctrl+J`).
  * Вертикальные и горизонтальные сплиты (`:vsplit`, `:split`, `Ctrl+W`).
  * Нечёткий поиск файлов (`Ctrl+P` / `:find`) и поиск по проекту (`:grep` + `Enter`).
  * Git-ветка в статус-лайне, `:git`, `:diff`, статистика проекта `:stats`, закладки (`m`/`M`), комментарии (`gcc`/`gc`), терминал (`:term`).
* **Встроенный менеджер плагинов (`catingiran Plug`)**: готовые пакеты (`python`, `c`, `java`, `web`, `git-extras`) и GitHub-плагины (`:PlugAdd user/repo`).
* **Гибкая настройка**: 35+ параметров в `~/.config/catingiran/catingiran.conf` и 7 тем (`charcoal`, `obsidian`, `transparent` / `onvim`, `catppuccin`, `tokyonight`, `nord`, `light`).

## Установка и удаление

```sh
git clone https://github.com/netanyahy/catingiran.git
cd catingiran
make
sudo make install   # на Gentoo с doas: doas make install
```

Запуск и личный конфиг:
```sh
cti                         # старт в текущей папке (с проводником файлов)
cti /path/to/file           # открыть файл без автозапуска боковой панели
cti /path/to/dir            # открыть проводник в указанной папке
cti -ot /dir /path/to/file  # открыть файл + сразу открыть проводник в /dir
```
Конфиг располагается в `~/.config/catingiran/catingiran.conf` (редактируется внутри IDE командой `:conf`, применяется без перезапуска через `:reload`).

Удаление:
```sh
sudo make uninstall
```

## Документация
* Подробная русская инструкция: [howtomakeru.md](howtomakeru.md)
* Full English guide: [howtomake.en](howtomake.en)
* Готовый пресет конфигурации: [catingiran.conf](catingiran.conf)
* Лицензия (BSD-3-Clause): [LICENSE](LICENSE)

## Авторы и контрибьюторы / Contributors
1. **[@netanyahy](https://github.com/netanyahy)** — автор идеи, создатель и мейнтейнер проекта
2. **Google Gemini** (`@gemini-code-assist`) — AI co-author (Antigravity)
