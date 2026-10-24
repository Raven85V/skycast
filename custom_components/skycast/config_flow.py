"""Мастер настройки: адрес станции."""
from __future__ import annotations

import aiohttp
import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import HomeAssistant

from .const import DOMAIN

STEP_USER = vol.Schema({
    vol.Required("host", default="192.168.1.240"): str,
    vol.Optional("name", default="SKYCAST"): str,
})


async def _check(hass: HomeAssistant, host: str) -> None:
    timeout = aiohttp.ClientTimeout(total=10)
    async with aiohttp.ClientSession(timeout=timeout) as session:
        async with session.get(f"http://{host}/api/state") as resp:
            resp.raise_for_status()
            data = await resp.json(content_type=None)
    if "weather" not in data:
        raise ValueError("нет поля weather")


class SkycastConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        errors = {}
        if user_input is not None:
            host = user_input["host"].strip()
            try:
                await _check(self.hass, host)
            except Exception:  # noqa: BLE001
                errors["base"] = "cannot_connect"
            else:
                await self.async_set_unique_id(host)
                self._abort_if_unique_id_configured()
                return self.async_create_entry(
                    title=user_input.get("name") or "SKYCAST",
                    data={"host": host, "name": user_input.get("name")},
                )
        return self.async_show_form(step_id="user", data_schema=STEP_USER, errors=errors)
