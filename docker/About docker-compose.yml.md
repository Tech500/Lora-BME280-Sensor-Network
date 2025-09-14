This docker-compose.yml uses persistent storage through volume mounts.

The persistent storage is configured in the volumes section:

./data:/app/data - Persists the database file (lora_sensors.db) and any other data files
./config:/app/config - Persists configuration files like settings.json
./logs:/app/logs - Persists application logs
./static:/app/static - Persists static files
These are bind mounts that map local directories on your host system to directories inside the container. This means that even if you stop and remove the container, all the data stored in these directories (./data, ./config, ./logs, ./static) will remain on your host system and be available when you recreate the container.

The database file specifically (lora_sensors.db) will be stored in the ./data directory on your host, ensuring your sensor data persists across container restarts and updates.

