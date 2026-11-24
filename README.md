# catingiran (`cti`)

Быстрая терминальная IDE и редактор кода на чистом C в стиле Vim/OnVim под лицензией **BSD-3-Clause**.

## Возможности
* **Проводник файлов как в VS Code** (`Ctrl+B` / `:ot` / `:ht`) с иконками Nerd Font и созданием/удалением/переименованием файлов.
* **Подсветка синтаксиса**: C/C++, Python, Java, JS/TS, Shell/Makefile, Conf/Markdown + 6 встроенных тем (`charcoal`, `obsidian`, `catppuccin`, `tokyonight`, `nord`, `light`).
* **Встроенный менеджер плагинов**: `plugin = user/repo` в `catingiran.conf`, команды `:PlugInstall`, `:PlugUpdate`, `:PlugStatus`, `:PlugClean`, `:PlugAdd`.
* **Уникальные IDE-инструменты**:
  * Встроенный запуск кода (`F5` / `:run`) для C, C++, Python, Java, Shell, Make с нижней панелью вывода (`Ctrl+J`).
  * Вертикальные и горизонтальные сплиты (`:vsplit`, `:split`, `Ctrl+W`).
  * Поиск файлов (`Ctrl+P` / `:find`) и поиск текста по проекту (`:grep` + переход по `Enter`).
  * Git-интеграция (ветка в статус-лайне, `:git`, `:diff`) и статистика кода (`:stats`).
  * Встроенный терминал (`:term`), закладки (`m` / `M`), комментирование (`gcc`), перемещение строк (`J`/`K` в Visual) и поддержка русской раскладки в Normal/Command режиме.

## Быстрая установка

```sh
git clone https://github.com/netanyahy/catingiran.git
cd catingiran
make
sudo make install
```

Запуск и редактирование настроек:
```sh
cti
```
Личный конфиг находится в `~/.config/catingiran/catingiran.conf` (или откройте из редактора командой `:conf`).

Удаление:
```sh
sudo make uninstall
```

## Документация
* Русская инструкция: [howtomakeru.md](howtomakeru.md)
* English guide: [howtomake.en](howtomake.en)
* Пресет конфига: [catingiran.conf](catingiran.conf)

