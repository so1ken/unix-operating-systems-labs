#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <new>
#include <pthread.h>
#include <string>
#include <utility>
#include <time.h>

class Event {
public:
    Event(unsigned int id, std::string payload)
        : id_(id), payload_(std::move(payload)) {}

    unsigned int id() const { return id_; }
    const std::string& payload() const { return payload_; }

private:
    unsigned int id_;
    std::string payload_;
};

class Monitor {
public:
    Monitor() : event_(nullptr), ready_(false), stopping_(false) {
        int error = pthread_mutex_init(&mutex_, nullptr);
        if (error != 0) {
            throw error;
        }

        error = pthread_cond_init(&condition_, nullptr);
        if (error != 0) {
            pthread_mutex_destroy(&mutex_);
            throw error;
        }
    }

    ~Monitor() {
        delete event_;
        pthread_cond_destroy(&condition_);
        pthread_mutex_destroy(&mutex_);
    }

    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    // Передаёт владение объектом монитору. Объект не сериализуется:
    // между потоками передаётся обычный указатель на экземпляр Event.
    bool provide(Event* event) {
        pthread_mutex_lock(&mutex_);

        while (ready_ && !stopping_) {
            pthread_cond_wait(&condition_, &mutex_);
        }

        if (stopping_) {
            pthread_mutex_unlock(&mutex_);
            return false;
        }

        event_ = event;
        ready_ = true;

        std::cout << "Поставщик: отправлено событие #" << event_->id()
                  << ", указатель " << static_cast<void*>(event_) << std::endl;

        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
        return true;
    }

    // Потребитель получает указатель и становится владельцем объекта.
    Event* consume() {
        pthread_mutex_lock(&mutex_);

        while (!ready_ && !stopping_) {
            pthread_cond_wait(&condition_, &mutex_);
        }

        if (!ready_ && stopping_) {
            pthread_mutex_unlock(&mutex_);
            return nullptr;
        }

        Event* event = event_;
        event_ = nullptr;
        ready_ = false;

        std::cout << "Потребитель: получено событие #" << event->id()
                  << ", данные: \"" << event->payload() << "\""
                  << std::endl;

        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
        return event;
    }

    void stop() {
        pthread_mutex_lock(&mutex_);
        stopping_ = true;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
    }

private:
    pthread_mutex_t mutex_{};
    pthread_cond_t condition_{};
    Event* event_;
    bool ready_;
    bool stopping_;
};

struct ThreadArgs {
    Monitor* monitor;
    unsigned int event_count;
};

static void sleep_one_second() {
    timespec remaining{1, 0};
    while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {
    }
}

static void* producer_thread(void* raw_args) {
    auto* args = static_cast<ThreadArgs*>(raw_args);

    for (unsigned int id = 1; id <= args->event_count; ++id) {
        sleep_one_second();

        Event* event = nullptr;
        try {
            event = new Event(id, "произвольный объект с данными события " +
                                      std::to_string(id));
        } catch (const std::bad_alloc&) {
            std::cerr << "Поставщик: не удалось выделить память для события" << std::endl;
            break;
        }

        if (!args->monitor->provide(event)) {
            delete event;
            break;
        }
    }

    return nullptr;
}

static void* consumer_thread(void* raw_args) {
    auto* args = static_cast<ThreadArgs*>(raw_args);

    while (true) {
        Event* event = args->monitor->consume();
        if (event == nullptr) {
            break;
        }

        // После consume() потребитель владеет объектом и освобождает его.
        delete event;
    }

    return nullptr;
}

static bool parse_event_count(const char* text, unsigned int* result) {
    char* end = nullptr;
    errno = 0;
    const unsigned long value = std::strtoul(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || value == 0 || value > 1000) {
        return false;
    }

    *result = static_cast<unsigned int>(value);
    return true;
}

int main(int argc, char** argv) {
    unsigned int event_count = 10;
    if (argc > 2 || (argc == 2 && !parse_event_count(argv[1], &event_count))) {
        std::cerr << "Использование: " << argv[0] << " [количество_событий_1..1000]"
                  << std::endl;
        return EXIT_FAILURE;
    }

    try {
        Monitor monitor;
        ThreadArgs args{&monitor, event_count};
        pthread_t consumer{};
        pthread_t producer{};

        int error = pthread_create(&consumer, nullptr, consumer_thread, &args);
        if (error != 0) {
            std::cerr << "pthread_create (потребитель): " << error << std::endl;
            return EXIT_FAILURE;
        }

        error = pthread_create(&producer, nullptr, producer_thread, &args);
        if (error != 0) {
            std::cerr << "pthread_create (поставщик): " << error << std::endl;
            monitor.stop();
            pthread_join(consumer, nullptr);
            return EXIT_FAILURE;
        }

        pthread_join(producer, nullptr);
        monitor.stop();
        pthread_join(consumer, nullptr);
    } catch (int error) {
        std::cerr << "Ошибка инициализации pthread: " << error << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
