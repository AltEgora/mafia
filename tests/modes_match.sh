#!/bin/sh
# Режимы нитей и корутин должны давать побайтно одинаковый вывод (в том числе
# интерактивный): у каждого бота свой генератор, а ведущий разбирает ответы по
# порядку номеров, поэтому способ исполнения игроков на ход партии не влияет.
# Использование: modes_match.sh путь/к/mafia путь/к/extended.yaml
set -e
mafia="$1"
extended="$2"
dir=$(dirname "$0")
for n in 5 8 13; do
    for seed in 1 2 3; do
        for extra in "" "--open-announcements" "--roles-config $extended" "--open-announcements --roles-config $extended"; do
            a=$("$mafia" --players $n --seed $seed --full-log --no-pauses $extra)
            b=$("$mafia" --players $n --seed $seed --full-log --no-pauses $extra --coroutines)
            [ "$a" = "$b" ] || { echo "различие: N=$n seed=$seed $extra"; exit 1; }
        done
    done
done
for seed in 1 2 3; do
    a=$(sh "$dir/interactive_smoke.sh" "$mafia" --players 9 --roles-config "$extended" --seed $seed)
    b=$(sh "$dir/interactive_smoke.sh" "$mafia" --players 9 --roles-config "$extended" --seed $seed --coroutines)
    [ "$a" = "$b" ] || { echo "различие в интерактивной партии: seed=$seed"; exit 1; }
done
echo "режимы совпадают"
