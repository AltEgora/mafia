#!/bin/sh
# --log-dir указывает внутрь обычного файла: каталог создать нельзя.
# Логи отключаются с предупреждением, а партия доигрывается до конца.
# Использование: log_failure_smoke.sh путь/к/mafia путь/к/обычному/файлу
set -e
mafia="$1"
blocker="$2"
out=$("$mafia" --players 7 --seed 1 --no-pauses --log-dir "$blocker")
echo "$out" | grep -q "Файловые логи отключены"
echo "$out" | grep -q "Игра окончена"
