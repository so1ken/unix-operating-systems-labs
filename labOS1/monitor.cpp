#include <iostream>
#include <string>
#include <pthread.h>
#include <unistd.h>

class Event {
public:
    int id;
    std::string payload;

    Event(int id) {
        this->id = id;
        payload = "message " + std::to_string(id);
    }
};

class Monitor {
private:
    pthread_mutex_t mutex;
    pthread_cond_t condition;

    Event* data = nullptr;
    bool ready = false;
    bool stopping = false;

public:
    Monitor() {
        pthread_mutex_init(&mutex, nullptr);
        pthread_cond_init(&condition, nullptr);
    }

    ~Monitor() {
        delete data;
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&condition);
    }

    void provide(Event* event) {
        pthread_mutex_lock(&mutex);

        while (ready && !stopping) {
            pthread_cond_wait(&condition, &mutex);
        }

        if (stopping) {
            pthread_mutex_unlock(&mutex);
            return;
        }

        data = event;
        ready = true;

        std::cout << "Producer: отправил событие "
                  << event->id << std::endl;

        pthread_cond_signal(&condition);
        pthread_mutex_unlock(&mutex);
    }

    Event* consume() {
        pthread_mutex_lock(&mutex);

        while (!ready && !stopping) {
            pthread_cond_wait(&condition, &mutex);
        }

        if (!ready) {
            pthread_mutex_unlock(&mutex);
            return nullptr;
        }

        Event* event = data;
        data = nullptr;
        ready = false;

        std::cout << "Consumer: получил событие "
                  << event->id << " (" << event->payload << ")"
                  << std::endl;

        pthread_cond_signal(&condition);
        pthread_mutex_unlock(&mutex);

        return event;
    }

    void stop() {
        pthread_mutex_lock(&mutex);
        stopping = true;
        pthread_cond_broadcast(&condition);
        pthread_mutex_unlock(&mutex);
    }
};

struct ThreadArgs {
    Monitor* monitor;
    int event_count;
};

void* producer(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;

    for (int id = 1; id <= args->event_count; id++) {
        sleep(1);

        Event* event = new Event(id);
        args->monitor->provide(event);
    }

    return nullptr;
}

void* consumer(void* arg) {
    Monitor* monitor = (Monitor*)arg;

    while (true) {
        Event* event = monitor->consume();

        if (event == nullptr) {
            break;
        }

        delete event;
    }

    return nullptr;
}

int main() {
    Monitor monitor;
    ThreadArgs args{&monitor, 5};

    pthread_t producerThread;
    pthread_t consumerThread;

    if (pthread_create(&consumerThread, nullptr, consumer, &monitor) != 0) {
        std::cerr << "Ошибка создания consumer" << std::endl;
        return 1;
    }

    if (pthread_create(&producerThread, nullptr, producer, &args) != 0) {
        monitor.stop();
        pthread_join(consumerThread, nullptr);
        std::cerr << "Ошибка создания producer" << std::endl;
        return 1;
    }

    pthread_join(producerThread, nullptr);

    monitor.stop();

    pthread_join(consumerThread, nullptr);

    return 0;
}