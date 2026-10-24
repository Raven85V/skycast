"""Опрос станции и отправка команд по HTTP."""
from __future__ import annotations

import asyncio
from datetime import timedelta
import logging

import aiohttp

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .const import DEFAULT_SCAN_INTERVAL, DOMAIN

_LOGGER = logging.getLogger(__name__)


class SkycastCoordinator(DataUpdateCoordinator[dict]):
    """Держит последний ответ /api/state и умеет отправлять команды."""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry) -> None:
        self.host = entry.data["host"].strip()
        self.station_name = entry.data.get("name") or "SKYCAST"
        super().__init__(
            hass,
            _LOGGER,
            name=DOMAIN,
            update_interval=timedelta(seconds=DEFAULT_SCAN_INTERVAL),
        )

    @property
    def base_url(self) -> str:
        return f"http://{self.host}"

    async def _async_update_data(self) -> dict:
        url = f"{self.base_url}/api/state"
        try:
            timeout = aiohttp.ClientTimeout(total=15)
            async with aiohttp.ClientSession(timeout=timeout) as session:
                async with session.get(url) as resp:
                    resp.raise_for_status()
                    data = await resp.json(content_type=None)
        except (aiohttp.ClientError, asyncio.TimeoutError) as err:
            raise UpdateFailed(f"станция недоступна: {err}") from err
        if not isinstance(data, dict) or "weather" not in data:
            raise UpdateFailed("неожиданный ответ станции")
        return data

    async def async_send_command(self, command: str, value: int | None = None) -> bool:
        """Отправить команду станции: /api/cmd?c=...&v=..."""
        params = {"c": command}
        if value is not None:
            params["v"] = str(value)
        url = f"{self.base_url}/api/cmd"
        try:
            timeout = aiohttp.ClientTimeout(total=10)
            async with aiohttp.ClientSession(timeout=timeout) as session:
                async with session.get(url, params=params) as resp:
                    resp.raise_for_status()
                    data = await resp.json(content_type=None)
        except (aiohttp.ClientError, asyncio.TimeoutError) as err:
            _LOGGER.error("Команда %s не отправлена: %s", command, err)
            return False
        ok = bool(data.get("ok"))
        if not ok:
            _LOGGER.error("Станция отклонила команду %s: %s", command, data)
        return ok
