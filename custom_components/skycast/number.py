"""Регуляторы станции (яркость)."""
from __future__ import annotations

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.const import PERCENTAGE
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import SkycastCoordinator
from . import SkycastConfigEntry


async def async_setup_entry(
    hass: HomeAssistant, entry: SkycastConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    device = DeviceInfo(identifiers={(DOMAIN, coordinator.host)})
    async_add_entities([SkycastBrightness(coordinator, device)])


class SkycastBrightness(CoordinatorEntity[SkycastCoordinator], NumberEntity):
    """Яркость экрана станции."""

    _attr_has_entity_name = False
    _attr_name = "Яркость экрана"
    _attr_icon = "mdi:brightness-6"
    _attr_native_min_value = 5
    _attr_native_max_value = 100
    _attr_native_step = 5
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_mode = NumberMode.SLIDER

    def __init__(self, coordinator: SkycastCoordinator, device: DeviceInfo) -> None:
        super().__init__(coordinator)
        self._attr_unique_id = f"{coordinator.host}_brightness"
        self._attr_device_info = device
        self._value = 80

    @property
    def native_value(self) -> float:
        return self._value

    async def async_set_native_value(self, value: float) -> None:
        self._value = int(value)
        await self.coordinator.async_send_command("brightness", int(value))
        self.async_write_ha_state()
