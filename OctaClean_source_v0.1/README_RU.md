# OctaClean v0.1 — Korg NTS-1 digital mkI

Экспериментальный stereo granular/time-domain pitch shifter для секции MOD FX.

## Управление

- A / TIME: высота тона от -24 до +24 полутонов, дискретно по полутонам.
- B / DEPTH: DRY/WET от 0% до 100% (equal-power crossfade).

## Как собрать официальным logue SDK

1. Клонируйте SDK и подмодули:

   git clone https://github.com/korginc/logue-sdk.git
   cd logue-sdk
   git submodule update --init

2. Скопируйте шаблон MOD FX:

   cp -r platform/nutekt-digital/dummy-modfx platform/nutekt-digital/OctaClean

3. Замените в созданной папке файлы `project.mk` и `manifest.json` файлами из этого архива и скопируйте туда `OctaClean.c`.

4. Соберите через официальный Docker environment или GNU Arm Embedded Toolchain. Для legacy-метода:

   cd platform/nutekt-digital/OctaClean
   make clean
   make
   make install

5. Получится `OctaClean.ntkdigunit`. Его можно установить через NTS-1 digital Librarian или logue-cli.

## Что делает алгоритм

Вход пишется в кольцевой stereo-буфер. Две read-head головки читают его с нужной скоростью и перекрываются в противофазе по циклу. Между отсчётами используется линейная интерполяция. Это даёт real-time pitch shift без изменения общей длительности потока, но характерные granular-артефакты на больших сдвигах возможны.

## Статус v0.1

Это первая тестовая версия. Главная задача — проверить поведение на реальном NTS-1, особенно -24 / -12 / 0 / +12 / +24 st и нагрузку DSP. После прослушивания имеет смысл подстроить длину окна/crossfade и затем сделать отдельный вариант `Pitch + Character`.
