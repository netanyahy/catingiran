# catingiran (`cti`)

Минималистичный терминальный редактор кода (TUI) на чистом C в стиле Vim/OnVim с боковой панелью проводника файлов как в VS Code, вкладками сверху, подсветкой синтаксиса (**C/C++**, **Python**, **Java**, JS/TS, Shell/Conf), настраиваемыми темами и конфигом `catingiran.conf`.

Лицензия: **BSD-3-Clause**.

## Быстрая установка

```sh
git clone https://github.com/netanyahy/catingiran.git
cd catingiran
make
sudo make install
```

Запуск:
```sh
cti
cti file.c
cti /path/to/project
```

Удаление:
```sh
sudo make uninstall
```

## Документация и настройка
* Русская инструкция: [howtomakeru.md](howtomakeru.md)
* English guide: [howtomake.en](howtomake.en)
* Конфиг по умолчанию: [catingiran.conf](catingiran.conf) (`~/.config/catingiran/catingiran.conf`)
