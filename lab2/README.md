# Лабораторная работа №2: Конкурентный контейнер

## Описание
Docker-контейнер для конкурентного управления файлами в общем томе.
Реализовано на POSIX Shell с использованием `flock` для атомарности операций.

## Требования
- Docker
- POSIX-совместимая оболочка

## Как собрать образ
```bash
    docker build -t file-manager .
```
## Как запустить и проверить?
### 1) Создать общий том
Том создаётся один раз и будет использоваться всеми контейнерами:
```bash
    docker volume create shared_data
```

### 2) Запустить один контейнер для проверки
```bash
    docker run --rm -v shared_data:/data file-manager
```
Остановить можно через Ctrl+C — сработает trap и удалит текущий файл

### 3)Запустить 10 или 50 контейнеров для проверки
```bash
    # 10 контейнеров
    for i in $(seq 1 10); do
        docker run --rm -d -v shared_data:/data file-manager
    done

    # 50 контейнеров
    for i in $(seq 1 50); do
        docker run --rm -d -v shared_data:/data file-manager
    done
```

### 4) Проверить результат
```bash
    # Посмотреть логи одного из работающих контейнеров
    docker logs $(docker ps -q | head -1)

    # Проверить, что том пуст после остановки
    docker stop $(docker ps -q)
    docker run --rm -v shared_data:/data alpine ls -la /data
```