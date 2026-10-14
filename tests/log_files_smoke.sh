#!/bin/sh
# Партия с --log-dir: каталог создаётся сам, в нём файлы раундов и итоговый файл.
# Использование: log_files_smoke.sh путь/к/mafia каталог путь/к/configs
set -e
mafia="$1"
dir="$2"
configs="$3"
rm -rf "$dir"
"$mafia" --players 9 --seed 7 --no-pauses --roles-config "$configs/extended.yaml" --log-dir "$dir" > /dev/null
test -s "$dir/game_7/round_01.txt"
test -s "$dir/game_7/summary.txt"
grep -q "Статистика по игрокам" "$dir/game_7/summary.txt"
grep -q "== Ночь 1 ==" "$dir/game_7/round_01.txt"
