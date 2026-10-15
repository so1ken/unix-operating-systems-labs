#!/bin/sh
VOLUME_DIR="/data"
LOCK_FILE="$VOLUME_DIR/lockfile"

CONTAINER_ID=$(cat /proc/sys/kernel/random/uuid)

FILE_COUNTER=0
CURRENT_FILE=""

cleanup() {
    if [ -n "$CURRENT_FILE" ] && [ -f "$CURRENT_FILE" ]; then
        rm -f "$CURRENT_FILE"
        echo "Trap: Удалён файл $CURRENT_FILE при выходе."
    fi
    exit 0
}
trap cleanup EXIT INT TERM

echo "Контейнер $CONTAINER_ID запущен."

while true; do
    FILE_COUNTER=$((FILE_COUNTER + 1))


    exec 200>"$LOCK_FILE"
    flock -x 200

    i=1
    while true; do
        num=$(printf '%03d' $i)
        filename="$VOLUME_DIR/$num"

        if [ ! -e "$filename" ]; then
            echo "$CONTAINER_ID $FILE_COUNTER" > "$filename"
            echo "Создан: $filename"
            CURRENT_FILE="$filename"
            break
        fi
        i=$((i + 1))
    done

    exec 200>&-
    sleep 1

    rm -f "$CURRENT_FILE"
    echo "Удалён: $CURRENT_FILE"
    CURRENT_FILE=""
done