#pragma once
#include <Arduino.h>
#include <time.h>

// Будильник по дням недели (логика как в PixelHub 75): файлы /alarms/NNN.mp3,
// играет по кругу до остановки касанием или автостопа. Громкость - день/ночь из настроек,
// первые секунды звук плавно нарастает.

void alarmTick(const tm& now);            // вызывать раз в секунду
bool alarmIsActive();
void alarmStop();                         // выключить сработавший будильник
void alarmInfo(int& hour, int& minute);   // время сработавшего будильника (для экрана)

// Проверка звука из настроек: играет/останавливает мелодию выбранного дня
void alarmTestToggle(int wday);
bool alarmTestActive();
void alarmTestStop();
