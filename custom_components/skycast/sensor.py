"""Сущности-датчики SKYCAST."""
from __future__ import annotations

from dataclasses import dataclass
from collections.abc import Callable

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.const import (
    PERCENTAGE,
    UnitOfPressure,
    UnitOfSpeed,
    UnitOfTemperature,
)
from homeassistant.core import HomeAssistant
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import SkycastCoordinator
from . import SkycastConfigEntry


@dataclass(frozen=True, kw_only=True)
class SkycastSensor(SensorEntityDescription):
    value: Callable[[dict], float | int | None]


def _w(data: dict) -> dict:
    return data.get("weather") or {}


DESCRIPTIONS: tuple[SkycastSensor, ...] = (
    SkycastSensor(
        key="temp", name="Температура улицы", native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        device_class=SensorDeviceClass.TEMPERATURE, state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: _w(d).get("temp")),
    SkycastSensor(
        key="feels", name="Ощущается", native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        device_class=SensorDeviceClass.TEMPERATURE, state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: _w(d).get("feels")),
    SkycastSensor(
        key="humidity", name="Влажность улицы", native_unit_of_measurement=PERCENTAGE,
        device_class=SensorDeviceClass.HUMIDITY, state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: _w(d).get("humidity")),
    SkycastSensor(
        key="pressure", name="Давление", native_unit_of_measurement="мм рт. ст.",
        device_class=SensorDeviceClass.PRESSURE, state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: _w(d).get("pressure_mmhg")),
    SkycastSensor(
        key="wind", name="Ветер", native_unit_of_measurement=UnitOfSpeed.METERS_PER_SECOND,
        device_class=SensorDeviceClass.WIND_SPEED, state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: _w(d).get("wind_ms")),
    SkycastSensor(
        key="wind_dir", name="Направление ветра", native_unit_of_measurement="°",
        state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: _w(d).get("wind_deg")),
    SkycastSensor(
        key="clouds", name="Облачность", native_unit_of_measurement=PERCENTAGE,
        state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: _w(d).get("clouds")),
    SkycastSensor(
        key="pop", name="Вероятность осадков", native_unit_of_measurement=PERCENTAGE,
        state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: _w(d).get("pop_now")),
    SkycastSensor(
        key="condition", name="Состояние", icon="mdi:weather-partly-cloudy",
        value=lambda d: _w(d).get("desc")),
)

HOME_DESCRIPTIONS: tuple[SkycastSensor, ...] = (
    SkycastSensor(
        key="home_temp", name="Температура дома", native_unit_of_measurement=UnitOfTemperature.CELSIUS,
        device_class=SensorDeviceClass.TEMPERATURE, state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: ((d.get("sensors") or {}).get("home") or {}).get("temp")),
    SkycastSensor(
        key="home_humidity", name="Влажность дома", native_unit_of_measurement=PERCENTAGE,
        device_class=SensorDeviceClass.HUMIDITY, state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: ((d.get("sensors") or {}).get("home") or {}).get("humidity")),
    SkycastSensor(
        key="home_pressure", name="Давление дома", native_unit_of_measurement=UnitOfPressure.HPA,
        device_class=SensorDeviceClass.PRESSURE, state_class=SensorStateClass.MEASUREMENT,
        value=lambda d: ((d.get("sensors") or {}).get("home") or {}).get("pressure_hpa")),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: SkycastConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    coordinator = entry.runtime_data
    device = DeviceInfo(
        identifiers={(DOMAIN, coordinator.host)},
        name=coordinator.station_name,
        manufacturer="DIY",
        model="ESP32-S3 Sunton 7 inch",
        configuration_url=coordinator.base_url,
    )

    entities: list[SkycastEntity] = [
        SkycastEntity(coordinator, d, device) for d in DESCRIPTIONS + HOME_DESCRIPTIONS
    ]

    for item in ((coordinator.data or {}).get("sensors") or {}).get("list") or []:
        sid = item.get("id")
        if not sid:
            continue
        label = item.get("label") or sid
        entities.append(SkycastEntity(
            coordinator,
            SkycastSensor(
                key=f"{sid}_temp", name=f"{label}: температура",
                native_unit_of_measurement=UnitOfTemperature.CELSIUS,
                device_class=SensorDeviceClass.TEMPERATURE,
                state_class=SensorStateClass.MEASUREMENT,
                value=(lambda k: lambda d: next(
                    (i.get("temp") for i in ((d.get("sensors") or {}).get("list") or []) if i.get("id") == k),
                    None))(sid)),
            device))
        entities.append(SkycastEntity(
            coordinator,
            SkycastSensor(
                key=f"{sid}_humidity", name=f"{label}: влажность",
                native_unit_of_measurement=PERCENTAGE,
                device_class=SensorDeviceClass.HUMIDITY,
                state_class=SensorStateClass.MEASUREMENT,
                value=(lambda k: lambda d: next(
                    (i.get("humidity") for i in ((d.get("sensors") or {}).get("list") or []) if i.get("id") == k),
                    None))(sid)),
            device))

    async_add_entities(entities)


class SkycastEntity(CoordinatorEntity[SkycastCoordinator], SensorEntity):
    """Одна сущность станции."""

    _attr_has_entity_name = False

    def __init__(self, coordinator: SkycastCoordinator, description: SkycastSensor, device: DeviceInfo) -> None:
        super().__init__(coordinator)
        self.entity_description = description
        self._attr_name = description.name
        self._attr_unique_id = f"{coordinator.host}_{description.key}"
        self._attr_device_info = device

    @property
    def native_value(self):
        try:
            return self.entity_description.value(self.coordinator.data or {})
        except Exception:  # noqa: BLE001
            return None
