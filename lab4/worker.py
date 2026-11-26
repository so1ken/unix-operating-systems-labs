import ast
import json
import math
import os
import socket
import time

import pika

RABBITMQ_HOST = os.getenv("RABBITMQ_HOST", "rabbitmq")
RABBITMQ_USER = os.getenv("RABBITMQ_USER", "calculator")
RABBITMQ_PASSWORD = os.getenv("RABBITMQ_PASSWORD", "calculator_password")
TASK_QUEUE = os.getenv("TASK_QUEUE", "calculations")
WORKER_NAME = socket.gethostname()
MAX_EXPRESSION_LENGTH = 256
MAX_AST_NODES = 50
MAX_ABS_RESULT = 1e100

BINARY_OPERATORS = {
    ast.Add: lambda left, right: left + right,
    ast.Sub: lambda left, right: left - right,
    ast.Mult: lambda left, right: left * right,
    ast.Div: lambda left, right: left / right,
    ast.FloorDiv: lambda left, right: left // right,
    ast.Mod: lambda left, right: left % right,
    ast.Pow: lambda left, right: left ** right,
}
UNARY_OPERATORS = {
    ast.UAdd: lambda value: +value,
    ast.USub: lambda value: -value,
}


def check_number(value):
    if type(value) not in (int, float):
        raise ValueError("Допускаются только целые и вещественные числа")
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError("Результат слишком велик")
    if abs(value) > MAX_ABS_RESULT:
        raise ValueError("Результат слишком велик")
    if isinstance(value, int) and value.bit_length() > 4096:
        raise ValueError("Слишком большое целое число")
    return value


def evaluate(expression):
    if not isinstance(expression, str) or not expression.strip():
        raise ValueError("Пустое выражение")
    if len(expression) > MAX_EXPRESSION_LENGTH:
        raise ValueError("Выражение слишком длинное")

    try:
        tree = ast.parse(expression, mode="eval")
    except SyntaxError as error:
        raise ValueError("Некорректное математическое выражение") from error

    if sum(1 for _ in ast.walk(tree)) > MAX_AST_NODES:
        raise ValueError("Слишком сложное выражение")

    def visit(node):
        if isinstance(node, ast.Expression):
            return visit(node.body)

        if isinstance(node, ast.Constant) and type(node.value) in (int, float):
            return check_number(node.value)

        if isinstance(node, ast.BinOp) and type(node.op) in BINARY_OPERATORS:
            left = visit(node.left)
            right = visit(node.right)
            if isinstance(node.op, ast.Pow) and abs(right) > 100:
                raise ValueError("Показатель степени должен быть от -100 до 100")
            return check_number(BINARY_OPERATORS[type(node.op)](left, right))

        if isinstance(node, ast.UnaryOp) and type(node.op) in UNARY_OPERATORS:
            return check_number(UNARY_OPERATORS[type(node.op)](visit(node.operand)))

        raise ValueError("Допустимы только числа, скобки и арифметические операции")

    try:
        return visit(tree)
    except ZeroDivisionError as error:
        raise ValueError("Деление на ноль") from error
    except OverflowError as error:
        raise ValueError("Результат слишком велик") from error


def rabbitmq_connection():
    credentials = pika.PlainCredentials(RABBITMQ_USER, RABBITMQ_PASSWORD)
    parameters = pika.ConnectionParameters(
        host=RABBITMQ_HOST,
        credentials=credentials,
        heartbeat=60,
        blocked_connection_timeout=30,
        connection_attempts=10,
        retry_delay=3,
    )
    return pika.BlockingConnection(parameters)


def process_message(channel, method, properties, body):
    try:
        request = json.loads(body.decode("utf-8"))
        result = evaluate(request.get("expression"))
        response = {"ok": True, "result": result, "worker": WORKER_NAME}
        print(
            f"[{WORKER_NAME}] {request['expression']} = {result}",
            flush=True,
        )
    except (ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        response = {"ok": False, "error": str(error), "worker": WORKER_NAME}
        print(f"[{WORKER_NAME}] Ошибка: {error}", flush=True)

    if properties.reply_to:
        channel.basic_publish(
            exchange="",
            routing_key=properties.reply_to,
            body=json.dumps(response, ensure_ascii=False).encode("utf-8"),
            properties=pika.BasicProperties(
                correlation_id=properties.correlation_id,
                content_type="application/json",
            ),
        )
    channel.basic_ack(delivery_tag=method.delivery_tag)


def run():
    while True:
        connection = None
        try:
            connection = rabbitmq_connection()
            channel = connection.channel()
            channel.queue_declare(queue=TASK_QUEUE, durable=True)
            channel.basic_qos(prefetch_count=1)
            channel.basic_consume(queue=TASK_QUEUE, on_message_callback=process_message)
            print(f"[{WORKER_NAME}] Ожидаю задания из очереди '{TASK_QUEUE}'", flush=True)
            channel.start_consuming()
        except (pika.exceptions.AMQPError, OSError) as error:
            print(f"[{WORKER_NAME}] Потеряно соединение: {error}. Повтор через 5 секунд", flush=True)
            time.sleep(5)
        finally:
            if connection is not None and connection.is_open:
                connection.close()


if __name__ == "__main__":
    run()
