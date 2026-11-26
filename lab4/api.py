import json
import os
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pika

RABBITMQ_HOST = os.getenv("RABBITMQ_HOST", "rabbitmq")
RABBITMQ_USER = os.getenv("RABBITMQ_USER", "calculator")
RABBITMQ_PASSWORD = os.getenv("RABBITMQ_PASSWORD", "calculator_password")
TASK_QUEUE = os.getenv("TASK_QUEUE", "calculations")
RESPONSE_TIMEOUT = float(os.getenv("RESPONSE_TIMEOUT", "15"))


def rabbitmq_connection():
    credentials = pika.PlainCredentials(RABBITMQ_USER, RABBITMQ_PASSWORD)
    parameters = pika.ConnectionParameters(
        host=RABBITMQ_HOST,
        credentials=credentials,
        heartbeat=60,
        blocked_connection_timeout=10,
        connection_attempts=5,
        retry_delay=2,
    )
    return pika.BlockingConnection(parameters)


def calculate(expression):
    correlation_id = str(uuid.uuid4())

    connection = rabbitmq_connection()
    try:
        channel = connection.channel()
        channel.queue_declare(queue=TASK_QUEUE, durable=True)
        reply_queue = channel.queue_declare(
            queue="", exclusive=True, auto_delete=True
        ).method.queue

        response = None

        def on_response(_channel, _method, properties, body):
            nonlocal response
            if properties.correlation_id == correlation_id:
                response = json.loads(body.decode("utf-8"))

        channel.basic_consume(
            queue=reply_queue,
            on_message_callback=on_response,
            auto_ack=True,
        )
        channel.basic_publish(
            exchange="",
            routing_key=TASK_QUEUE,
            body=json.dumps({"expression": expression}).encode("utf-8"),
            properties=pika.BasicProperties(
                reply_to=reply_queue,
                correlation_id=correlation_id,
                delivery_mode=2,
                content_type="application/json",
            ),
        )

        deadline = time.monotonic() + RESPONSE_TIMEOUT
        while response is None and time.monotonic() < deadline:
            connection.process_data_events(time_limit=0.2)

        if response is None:
            raise TimeoutError("Время ожидания вычислителя истекло")
        return response
    finally:
        if connection.is_open:
            connection.close()


def send_json(handler, status, data):
    body = json.dumps(data, ensure_ascii=False).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


class CalculatorHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/health":
            send_json(self, 200, {"status": "ok", "service": "calculator-api"})
            return
        send_json(
            self,
            200,
            {
                "service": "distributed-calculator",
                "usage": "POST /calculate",
                "example": {"expression": "2 + 3 * (10 - 4) ** 2"},
            },
        )

    def do_POST(self):
        if self.path != "/calculate":
            send_json(self, 404, {"error": "Маршрут не найден"})
            return

        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if content_length <= 0 or content_length > 4096:
                send_json(self, 400, {"error": "Некорректный размер запроса"})
                return
            request = json.loads(self.rfile.read(content_length).decode("utf-8"))
            expression = request.get("expression") if isinstance(request, dict) else None
            if not isinstance(expression, str) or not expression.strip():
                send_json(self, 400, {"error": "Передайте строковое поле expression"})
                return

            result = calculate(expression)
            if result.get("ok"):
                send_json(
                    self,
                    200,
                    {
                        "expression": expression,
                        "result": result["result"],
                        "worker": result["worker"],
                    },
                )
            else:
                send_json(
                    self,
                    400,
                    {"error": result.get("error", "Не удалось вычислить выражение")},
                )
        except json.JSONDecodeError:
            send_json(self, 400, {"error": "Тело запроса должно содержать JSON"})
        except TimeoutError as error:
            send_json(self, 504, {"error": str(error)})
        except (pika.exceptions.AMQPError, OSError) as error:
            print(f"Ошибка подключения к RabbitMQ: {error}", flush=True)
            send_json(self, 503, {"error": "Брокер сообщений недоступен"})
        except (UnicodeDecodeError, ValueError) as error:
            send_json(self, 400, {"error": str(error)})


if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", 8000), CalculatorHandler)
    print("API запущен на порту 8000", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Остановка API", flush=True)
    finally:
        server.server_close()
