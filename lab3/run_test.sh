#!/bin/sh
# Скрипт для создания тестового окружения
TEST_DIR="test_dir"

echo "Подготовка тестового окружения..."

# Если папка уже есть, удаляем её
if [ -d "$TEST_DIR" ]; then
    rm -rf "$TEST_DIR"
fi

# Создаем папку
mkdir -p "$TEST_DIR"

# Создаем оригинальный файл
echo "UNIX" > "$TEST_DIR/file1.txt"

# Создаем дубликаты - копии
cp "$TEST_DIR/file1.txt" "$TEST_DIR/file2.txt"
cp "$TEST_DIR/file1.txt" "$TEST_DIR/file3.txt"

# Создаем уникальный файл с другим содержимым
echo "different" > "$TEST_DIR/unique.txt"

echo "Готово, тестовое окружение создано:"
ls -l "$TEST_DIR" 