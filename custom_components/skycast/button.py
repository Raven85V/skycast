"""Кнопки управления станцией."""
from __future__ import annotations

from dataclasses import dataclass

from homeassistant.components.button import ButtonEntity, ButtonEntityDescription
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import SkycastCoordinator
from . import SkycastConfigEntry


@dataclass(frozen=True, kw_only=True)
class SkycastButton(ButtonEntityDescription):
    command: str


BUTTONS: tuple[SkycastButton, ...] = (
    SkycastButton(key="screen", name="Сменить экран", icon="mdi:page-next-outline", command="screen"),
    SkycastButton(key="home_screen", name="Экран Дома", icon="mdi:home-analytics", command="home"),
    SkycastButton(key="clock_screen", name="Большие часы", icon="mdi:clock-outline", command="clock"),
    SkycastButton(key="reboot", name="Перезагрузка", icon="mdi:restart", command="reboot"),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: SkycastConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    device = DeviceInfo(identifiers={(DOMAIN, coordinator.host)})
    async_add_entities(SkycastButtonEntity(coordinator, d, device) for d in BUTTONS)


class SkycastButtonEntity(CoordinatorEntity[SkycastCoordinator], ButtonEntity):
    """Кнопка, отправляющая команду станции."""

    _attr_has_entity_name = False
    entity_description: SkycastButton

    def __init__(self, coordinator: SkycastCoordinator, description: SkycastButton, device: DeviceInfo) -> None:
        super().__init__(coordinator)
        self.entity_description = description
        self._attr_name = description.name
        self._attr_unique_id = f"{coordinator.host}_btn_{description.key}"
        self._attr_device_info = device

    async def async_press(self) -> None:
        await self.coordinator.async_send_command(self.entity_description.command)
        await self.coordinator.async_request_refresh()
