#pragma once

void uiSettingsOpen();    // нарисовать экран настроек (вызвать один раз при входе)
bool uiSettingsLoop();    // вызывать в loop(); false - экран закрыт (кнопка "Назад" или 60 с без касаний)
