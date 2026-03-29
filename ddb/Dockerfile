FROM python:3.11-slim
WORKDIR /app
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

COPY . .
RUN pip install --upgrade pip && pip install -e .

EXPOSE 8080
CMD ["darbotdb-api"]
