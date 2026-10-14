#!/bin/sh
# Интерактивная партия, где «человек» перебирает ответы всех форматов:
# неподходящие ведущий отклоняет и переспрашивает. Партия должна дойти до конца.
# Использование: interactive_smoke.sh путь/к/mafia [доп. параметры]
set -e
mafia="$1"
shift
i=0
while :; do
    n=$((i % 8))
    echo "$n"
    echo "п $n"
    echo "в $n"
    echo "$n $(((n + 1) % 8))"
    i=$((i + 1))
done | "$mafia" --players 7 --interactive --open-announcements --full-log --no-pauses --seed 3 "$@"
