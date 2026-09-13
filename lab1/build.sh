#!/bin/sh
file="$1"
outputName=""

if [ $# -eq 0 ]; then
    echo "Ошибка: укажите файл"
    exit 1
fi

if [ ! -r "$file" ]; then
    echo "Ошибка: файл не читается"
    exit 2
fi

outputName=$(grep -m 1 "Output:" "$file" | sed 's/.*Output:[[:space:]]*//')

if [ -z "$outputName" ]; then
    echo "Ошибка: не найден комментарий"
    exit 3
fi

tmpDir=$(mktemp -d)

clean(){
    rm -rf "$tmpDir"
}

trap clean EXIT INT TERM

ext="${file##*.}"

if [ "$ext" = "c" ]; then
    cc "$file" -o "$tmpDir/$outputName"
elif [ "$ext" = "cpp" ]; then
    c++ "$file" -o "$tmpDir/$outputName"
elif [ "$ext" = "tex" ]; then
    baseName=$(basename "$file" .tex)

    pdflatex -output-directory "$tmpDir" "$file"
    rc=$?
    
    if [ $rc -eq 0 ]; then
        mv "$tmpDir/$baseName.pdf" "$tmpDir/$outputName.pdf"
        outputName="$outputName.pdf"
    fi
else
    echo "Ошибка: неизвестный формат файла"
    exit 4
fi  

if [ $? -ne 0 ]; then
    echo "Ошибка компиляции"
    exit 5
fi

srcDir=$(dirname "$file")
mv "$tmpDir/$outputName" "$srcDir/$outputName"

echo "Сборка завершена успешно"
exit 0