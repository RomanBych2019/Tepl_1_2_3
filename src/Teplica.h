#pragma once

#include "MB11016P_ESP.h"
#include "Sensor_WB_v_3.h"
#include "Window.h"
#include "Heat.h"

class Teplica 
{
private:
    int _mode, _id;
    int _oldtemperatute, _setpump, _setheat, _setwindow, _relayPump, _relayHeat, _relayUp, _relayDown, _channel_t;
    int _hysteresis = 20, _counter = 0;
    int32_t _correctiontemp, _temperature, _temperatureIntegral, _temperatureIntegralUp, _temperatureIntegralDown;
    bool _there_are_windows = true;
    unsigned long _time_air, _time_decrease_in_humidity, _time_close_window;
    const uint32_t _WAITING_ON_PUMP = 300000;
    const uint AIRLEVEL = 20;
    MB11016P_ESP *__relay = nullptr;
    Heat *__heat = nullptr;
    Window *__window = nullptr;
    Sensor_WB_v_3 *__sensor;

public:
    // Ошибки датчиков температуры
    static const uint NO_ERROR = 1;
    static const uint NO_POWER = 0xffff;

    // конструктор для WB_v_3 (датчик температуры) и MB110_16P (блок реле)
    Teplica(int id, Sensor_WB_v_3 *Sensor_WB, int relayPump, int relayHeat, int relayUp, int relayDown,
            int setpump, int setheat, int setwindow, int opentimewindows, MB11016P_ESP *mb11016p, Preferences *flash) : 
            _mode(AUTO), _id(id), _setpump(setpump), _setheat(setheat), _setwindow(setwindow), _relayPump(relayPump),
            _relayHeat(relayHeat), _relayUp(relayUp), _relayDown(relayDown)
    {
        __window = new Window(mb11016p, relayUp, relayDown, opentimewindows, flash);
        __relay = mb11016p;
        __sensor = Sensor_WB;
        _oldtemperatute = setpump;
    }
  
    enum mode
    {
        AUTO,                // автоматический режим
        MANUAL,              // ручной режим
        AIR,                 // проветривание
        DECREASE_IN_HUMIDITY // осушение
    };

   void regulationPump(int temperature) const
    {
        if (getMode() == AUTO)
        {            
            // если теплица с окнами
            if (getLevel() <= 10)
            {
                // управление включением нагревателей
                if (_setpump - temperature > _hysteresis >> 1)
                    if (millis() > _time_close_window)
                    {
                        __relay->setOn(_relayPump);
                    }
                // управление дополнительным нагревателем
                if (_setheat - temperature > _hysteresis >> 1)
                {
                    __relay->setOn(_relayHeat);
                }
            }
            // управление выключением нагревателей
            if (temperature > _setpump)
            {
                __relay->setOff(_relayPump);
            }
            // управление дополнительным нагревателем
            if (temperature > _setheat)
            {
                __relay->setOff(_relayHeat);
            }
        }
    }
    // управление окнами
    void regulationWindow(int temperature, int outdoortemperature = 0, int rain = 0)
    {
        if (_temperatureIntegral < 0 && temperature > _setwindow)
        {
            _temperatureIntegral = 0;
        }
        if (_temperatureIntegral > 0 && temperature < _setwindow)
        {
            _temperatureIntegral = 0;
        }
        if (getMode() == AUTO)
        {
            int Tout = 0;
            if (outdoortemperature)
                Tout = constrain(map(outdoortemperature - _setpump, -1000, 1000, -5, 10), -5, 10);
            _temperatureIntegral += temperature - _setwindow;
            _counter++;
            // условие открытия окон
            if (!rain || getLevel() < 25) // если не идет дождь или открыто менее чем на 25%
            {
                if (temperature > _setwindow + 500) // если очень жарко
                {
                    __window->openWindow(30, _id);
                    _temperatureIntegral = 0;
                    _counter = 0;
                }
                else if (_oldtemperatute <= temperature) // если температура не падает
                {
                    if (temperature > _setwindow + 250 && __window->getlevel() < 20)
                    {
                        __window->openWindow(20 + Tout, _id);
                        _temperatureIntegral = 0;
                    }
                    else if (temperature > _setwindow + 50)
                        if (_temperatureIntegral > 100) //
                        {
                            __window->openWindow(constrain(map(_counter, 1, 16, 12, 5), 5, 15) + Tout, _id);
                            _temperatureIntegral = 0;
                            _counter = 0;
                        }
                }
            }
            // условие закрытия окон
            if (temperature <= _setpump + _hysteresis)
            {
                if (_oldtemperatute >= temperature) //
                {
                    if (__window->getlevel() > 30) // прикрытие  окон при достижении температуры уставки
                    {
                        __window->closeWindow(getLevel() / 2, _id); //
                    }
                    else // закрытие окон при достижении температуры уставки
                    {
                        if (__window->getlevel() > 10)
                            _time_close_window = millis() + _WAITING_ON_PUMP; // задержка последующего пуска нагревателя
                        __window->closeWindow(getLevel() + 5, _id);
                    }
                }
            }
            else if (getLevel())
            {
                if (temperature < _setwindow && _oldtemperatute - temperature > 150)
                {
                    __window->closeWindow(constrain(map(_counter, 1, 16, 15, 5), 5, 15) - Tout, _id);
                    _temperatureIntegral = 0;
                    _counter = 0;
                }
                else if (temperature < _setwindow && _oldtemperatute >= temperature)
                    if (_temperatureIntegral < -100) //
                    {
                        __window->closeWindow(constrain(map(_counter, 1, 16, 15, 5), 5, 15) - Tout, _id);
                        _temperatureIntegral = 0;
                        _counter = 0;
                    }
            }
        }
        // Serial.printf("Tepl %d, temper: %d\n", getAdressT(), temperature);
        // Serial.printf("Tepl %d, oldtemper: %d\n", getAdressT(), _oldtemperatute);
        // Serial.printf("Tepl %d, temperSet: %d\n", getAdressT(), _setwindow);
        // Serial.printf("Tepl %d, integral: %d\n", getAdressT(), _temperatureIntegral);
        // Serial.printf("Tepl %d, coun: %d\n", getAdressT(), _counter);
        // Serial.printf("Tepl %d, level: %d\n\n", getAdressT(), getLevel());
        _oldtemperatute = temperature;
    }

    // режим проветривания
    void air(unsigned long airtime, int outdoortemperature)
    {
        _time_air = millis() + airtime;
        if (!_there_are_windows)
        {
            __window->openWindow(100, _id);
        }
        else if (__relay->getRelay(_relayUp) || __relay->getRelay(_relayDown))
            return;
        else if (getLevel() < AIRLEVEL)
        {
            __window->openWindow(AIRLEVEL - getLevel(), _id);
        }
    }

    // режим осушения
    void decrease_in_humidity(unsigned long heattime, int outdoortemperature)
    {
        _time_decrease_in_humidity = millis() + heattime;
        __relay->setOn(_relayPump);
        if (!_there_are_windows)
        {
            __window->openWindow(100, _id);
        }
        else if (__relay->getRelay(_relayUp) || __relay->getRelay(_relayDown))
            return;
        else if (getLevel() < AIRLEVEL)
            __window->openWindow(AIRLEVEL - getLevel(), _id);
    }
    void updateWorkWindows()
    {
        __window->off();
        if (_mode == AIR && _time_air < millis())
        {
            _mode = AUTO;
        }
        if (_mode == DECREASE_IN_HUMIDITY && _time_decrease_in_humidity < millis())
        {
            _mode = AUTO;
        }
        if (_mode == AIR || _mode == DECREASE_IN_HUMIDITY)
            if (getLevel() < AIRLEVEL)
                __window->openWindow(AIRLEVEL - getLevel(), _id);
        alarm();
    }
    void alarm()
    {
        if (!_there_are_windows)
        {
            if (Sensor_WB_v_3::NO_ERROR == getSensorStatus())
            {
                if (getTemperature() <= _setheat + 100)
                    _mode = AUTO;
                if (getTemperature() - _setwindow > 300)
                    _mode = AUTO;
            }
            return;
        }
        if (_mode == AIR || _mode == DECREASE_IN_HUMIDITY)
        {
            if (getTemperature() < _setheat + 100)
            {
                _mode = AUTO;
                __window->closeWindow(getLevel(), _id);
            }
            return;
        }

        if (Sensor_WB_v_3::NO_ERROR == getSensorStatus())
            if (getTemperature() <= _setheat)
            {
                _mode = AUTO;
                __window->closeWindow(getLevel(), _id);
            }
    }

    // установка уставки насос
    void setSetPump(int setpoint)
    {
        _temperatureIntegral = 0;
        _setpump = setpoint;
        _oldtemperatute = getTemperature();
    }
    // установка уставки доп.нагреватель
    void setSetHeat(int setpoint)
    {
        _temperatureIntegral = 0;
        _setheat = setpoint;
        _oldtemperatute = getTemperature();
    }
    // установка уставки окна
    void setSetWindow(int setpoint)
    {
        _temperatureIntegral = 0;
        _setwindow = setpoint;
        _oldtemperatute = getTemperature();
    }
    int getSetPump() const
    {
        return _setpump;
    }
    int getSetHeat() const
    {
        return _setheat;
    }
    int getSetWindow() const
    {
        return _setwindow;
    }

    // установка режима гистерезиста насосов
    void setHysteresis(int hysteresis)
    {
        _hysteresis = hysteresis;
    }
    int getHysteresis() const
    {
        return _hysteresis;
    }
    int getAdressT() const
    {
        return __sensor->getAdress();
    }
    // установка уровня окон
    void setWindowlevel(int changelevel) const
    {
        
        changelevel -= __window->getlevel();
        if (changelevel > 0)
            __window->openWindow(changelevel, _id);
        if (changelevel < 0)
            __window->closeWindow(-changelevel, _id);
    }
    void setMode(int mode)
    {
        _mode = mode;
    }
    int getMode() const
    {
        return _mode;
    }
    int getTemperature() const
    {
        return __sensor->getTemperature();
    }
    int getSensorStatus()
    {
        return __sensor->getStatus();
    }
    int getLevel() const
    {
        return __window->getlevel();
    }
    int getOpenTimeWindow() const
    {
        if (!_there_are_windows)
            return 0;
        return __window->getOpenTimeWindow();
    }
    int getHumidity() const
    {
        return __sensor->getHumidity();
    }
    int getPump() const
    {
        return __relay->getRelay(_relayPump);
    }
    void setPump(int vol) const
    {
        vol ? __relay->setOn(_relayPump) : __relay->setOff(_relayPump);
    }
    int getHeat()
    {
        return __relay->getRelay(_relayHeat);
    }
    void setHeat(int vol)
    {
        vol ? __relay->setOn(_relayHeat) : __relay->setOff(_relayHeat);
    }
    int getWindowUp()
    {
        if (!_there_are_windows)
            return -1;
        return __window->getWindowDown();
    }
    int getWindowDown()
    {
        if (!_there_are_windows)
            return -1;
        return __window->getWindowDown();
    }

    void setAdress(int adr)
    {
        __sensor->setAdress(adr);
    }
    void setCorrectionTemp(int correctiontemp)
    {
        __sensor->setCorrectionTemp(correctiontemp);
    }
    int getCorrectionTemp()
    {
        return __sensor->getCorrectionTemp();
    }
    int getId()
    {
        return _id;
    }
    void setOpenTimeWindow(int time)
    {
        if (!_there_are_windows)
            return;
        __window->setopentimewindow(time);
    }
    bool getThereAreWindows()
    {
        return _there_are_windows;
    }
};