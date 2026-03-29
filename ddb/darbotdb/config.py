from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    # ArangoDB connection
    DDB_HOSTS: str = "http://darbotdb:8529"
    DDB_DATABASE: str = "txt2kg"
    DDB_USERNAME: str = "root"
    DDB_PASSWORD: str = ""

    # FastAPI server
    API_PORT: int = 8080

    # Darango service (standalone darango API, if running separately)
    DARANGO_SERVICE_URL: str = "http://localhost:8081"

    # txt2kg / Ollama integration (mirrors darango router defaults)
    TXT2KG_URL: str = "http://10.1.8.69:3001"
    TXT2KG_DDB_URL: str = "http://10.1.8.69:8529"
    TXT2KG_DDB_NAME: str = "txt2kg"
    TXT2KG_OLLAMA_URL: str = "http://10.1.8.69:11434"
    TXT2KG_MODEL: str = "qwen3:32b"

    # Micro DB data root (relative path from working directory)
    MICRO_DATA_ROOT: str = "data"


settings = Settings()  # loads from env/.env if present