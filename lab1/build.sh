#!/bin/sh
set -e  # Выходить при любой ошибке

# Функция обработки исключений и вывода их в stderr
exceptions(){
    echo "Ошибка: $1" >&2
    exit "$2"
}

file="$1"
outputName=""

if [ $# -eq 0 ]; then exceptions "укажите файл" 1; fi

# Получаем абсолютный путь к файлу и папке, где он лежит
abs_file=$(realpath "$file")
src_dir=$(dirname "$abs_file")

if [ ! -r "$abs_file" ]; then exceptions "файл не читается" 2; fi

# Добавил || true так как, в случае отсутствия Output, grep выдаст код ошибки 1 и программа немедленно завершится, до следующего if [ -z "$outputName" ]
outputName=$(grep -m 1 "Output:" "$abs_file" | sed 's/.*Output:[[:space:]]*//' || true)

if [ -z "$outputName" ]; then die "не найден комментарий Output:" 3; fi

tmpDir=$(mktemp -d)

# Исправленная обработка сигналов
exit_handler() {
    local rc=$?
    trap - EXIT
    rm -rf "$tmpDir"
    exit $rc
}
trap exit_handler EXIT HUP INT QUIT PIPE TERM

# Переходим во временный каталог
cd "$tmpDir" || exit 1

ext="${abs_file##*.}"

if [ "$ext" = "c" ]; then
    if ! cc "$abs_file" -o "$outputName"; then exceptions "компиляции C" 4; fi
elif [ "$ext" = "cpp" ]; then
    if ! c++ "$abs_file" -o "$outputName"; then exceptions "компиляции C++" 4; fi
elif [ "$ext" = "tex" ]; then
    baseName=$(basename "$abs_file" .tex)

    if ! pdflatex -interaction=nonstopmode "$abs_file" > /dev/null; then 
        exceptions "компиляции LaTeX" 4
    fi
    
    mv "$baseName.pdf" "$outputName.pdf"
    outputName="$outputName.pdf"
else
    exceptions "неизвестный формат файла" 5
fi 

mv "$outputName" "$src_dir/$outputName"

echo "Сборка завершена успешно"
exit 0