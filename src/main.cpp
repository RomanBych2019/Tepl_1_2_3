#include "main.h"

void setup()
{
  pinMode(GPIO_NUM_2, OUTPUT);

  Serial.begin(115200);
  Serial.print(__DATE__);
  hmi.echoEnabled(true);
  hmi.hmiCallBack(onHMIEvent);

  hmi("rest");

  wifiInit();

  if (!LittleFS.begin())
    Serial.println("FS Error");

  ui.attachBuild(buildPage);
  ui.attach(actionPage);
  ui.start("Greenhouse_Tepl_1-3");
  // ui.downloadAuto(0); // отключить авто скачивание
  ui.enableOTA();

#ifdef DEBUG_WIFI
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("Connect to:\t");
    Serial.println(ssid);
    Serial.printf("IP address:\t");
    Serial.println(WiFi.localIP());
    Serial.printf("Hostname:\t");
    Serial.println(WiFi.getHostname());
    Serial.printf("Mac Address:\t");
    Serial.println(WiFi.macAddress());
    Serial.printf("Subnet Mask:\t");
    Serial.println(WiFi.subnetMask());
    Serial.printf("Gateway IP:\t");
    Serial.println(WiFi.gatewayIP());
    Serial.printf("DNS:t\t\t");
    Serial.println(WiFi.dnsIP());
    Serial.println("HTTP server started");
  }
  else
    Serial.printf("No connect WiFi\n");
#endif
#ifdef SCAN_WIFI
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  for (;;)
  {
    Serial.println("scan start");
    // WiFi.scanNetworks will return the number of networks found
    int n = WiFi.scanNetworks();
    Serial.println("scan done");
    if (n == 0)
    {
      Serial.println("no networks found");
    }
    else
    {
      Serial.print(n);
      Serial.println(" networks found");
      for (int i = 0; i < n; ++i)
      {
        // Print SSID and RSSI for each network found
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(WiFi.SSID(i));
        Serial.print(" (");
        Serial.print(WiFi.RSSI(i));
        Serial.print(")");
        Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
        delay(10);
      }
    }
    Serial.println("");
    // Wait a bit before scanning again
    delay(5000);
  }
#endif

  flash.begin("eerom", false);
  MbMasterSerial.begin(flash.getInt("mspeed", 19200), SERIAL_8N1, RXDMASTER, TXDMASTER, false); // Modbus Master
  MbSlaveSerial.begin(flash.getInt("sspeed", 19200));                                          // Modbus Slave
  SerialNextion.begin(19200, SWSERIAL_8N1, RXDNEX, TXDNEX, false);

  mbsl8di8ro.setAdress(flash.getInt("heat_adr", 102));

  String incStr;
  slave.begin(&MbSlaveSerial);
  slave.slave(IDSLAVE);

  slaveWiFi.slave(IDSLAVE);
  slaveWiFi.begin();

  mb_master.begin(&MbMasterSerial);
  mb_master.master();

  for (int i = rs485mode1; i < rs485_HOLDING_REGS_SIZE; i++)
    slave.addHreg(i);
  for (int i = WiFimode1; i < WiFi_HOLDING_REGS_SIZE; i++)
    slave.addIreg(i);
  for (int i = wifi_flag_edit_1; i < wifi_HOLDING_REGS_SIZE; i++)
    slave.addHreg(i);

  arr_Tepl[0] = &Tepl1;
  arr_Tepl[1] = &Tepl2;
  arr_Tepl[2] = &Tepl3;

  sensors[0] = &Tepl1Temperature;
  sensors[1] = &Tepl2Temperature;
  sensors[2] = &Tepl3Temperature;

  // считывание параметров установок теплиц из памяти
  for (int i = 0; i < 3; i++)
  {
    uint t = flash.getUInt(String("SetPump" + String(arr_Tepl[i]->getId())).c_str(), 500);
    arr_Tepl[i]->setSetPump(t);
    t = flash.getUInt(String("SetHeat" + String(arr_Tepl[i]->getId())).c_str(), 300);
    arr_Tepl[i]->setSetHeat(t);
    t = flash.getUInt(String("SetSetWindow" + String(arr_Tepl[i]->getId())).c_str(), 600);
    arr_Tepl[i]->setSetWindow(t);
    t = flash.getUInt(String("Hyster" + String(arr_Tepl[i]->getId())).c_str(), 20);
    arr_Tepl[i]->setHysteresis(t);
    t = flash.getUInt(String("Opentwin" + String(arr_Tepl[i]->getId())).c_str(), 60);
    arr_Tepl[i]->setOpenTimeWindow(t);
  }

  incStr = flash.getString("adr", "");
  pars_str_adr(incStr);

  // первоначальное закрытие окон
  Tepl1.setWindowlevel(-100);
  Tepl2.setWindowlevel(-100);
  Tepl3.setWindowlevel(-100);

  xTaskCreatePinnedToCore(
      update_WiFiConnect,        /* Обновление WiFi */
      "Task_update_WiFiConnect", /* Название задачи */
      4096,                      /* Размер стека задачи */
      NULL,                      /* Параметр задачи */
      1,                         /* Приоритет задачи */
      NULL,                      /* Идентификатор задачи, чтобы ее можно было отслеживать */
      0);                        /* Ядро для выполнения задачи (0) */

  xTaskCreatePinnedToCore(
      updateMB,        /* Обновление состояния реле блока MB110 */
      "Task_updateMB", /* Название задачи */
      4096,            /* Размер стека задачи */
      NULL,            /* Параметр задачи */
      3,               /* Приоритет задачи */
      NULL,            /* Идентификатор задачи, чтобы ее можно было отслеживать */
      tskNO_AFFINITY); /* Ядро для выполнения задачи (0) */

  xTaskCreatePinnedToCore(
      sendNextion,        /* обновление данных HMI */
      "Task_sendNextion", /* Название задачи */
      8192,               /* Размер стека задачи */
      NULL,               /* Параметр задачи */
      4,                  /* Приоритет задачи */
      NULL,               /* Идентификатор задачи, чтобы ее можно было отслеживать */
      1);

  xTaskCreatePinnedToCore(
      readNextion,        /* чтение данных от HMI */
      "Task_readNextion", /* Название задачи */
      8192,               /* Размер стека задачи */
      NULL,               /* Параметр задачи */
      2,                  /* Приоритет задачи */
      NULL,               /* Идентификатор задачи, чтобы ее можно было отслеживать */
      tskNO_AFFINITY);

  updateNextion = millis();

  xTaskCreatePinnedToCore(
      updateGreenHouse,        /* Регулировка окон*/
      "Task_updateGreenHouse", /* Название задачи */
      10000,                   /* Размер стека задачи */
      NULL,                    /* Параметр задачи */
      2,                       /* Приоритет задачи */
      &Task_updateGreenHouse,  /* Идентификатор задачи, чтобы ее можно было отслеживать */
      tskNO_AFFINITY);
}

void loop()
{
  if (flag_start) // установка окон в положение записанное в последний раз перед выключением (один раз после перезагрузки)
  {
    if (!Tepl1.getWindowDown() && !Tepl2.getWindowDown() && !Tepl3.getWindowDown())
    {
      for (Teplica *t : arr_Tepl)
      {
        int tmp = flash.getUInt(String("LevelWindow" + String(t->getId())).c_str(), 0);
        t->setWindowlevel(tmp);
      }
      flag_start = false;
    }
  }
  ui.tick();
  saveOutModBusArr();
  slave.task();
  slaveWiFi.task();
  heat.update();
  controlScada();
  if (millis() > 10000)
    for (Teplica *t : arr_Tepl)
    {
      t->updateWorkWindows();
      if (1 == t->getSensorStatus())
        t->regulationPump(t->getTemperature());
    }
}

void readNextion(void *pvParameters)
{
  for (;;)
  {
    hmi.listen();
    // vPrintString("readNextion");
    // vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TIME_UPDATE_HMI));
    vTaskDelay(pdMS_TO_TICKS(TIME_UPDATE_HMI));
  }
  vTaskDelete(NULL);
}

void sendNextion(void *pvParameters)
{
  for (;;)
  {
    if (pageNextion == "p0")
    {
      // TimingUtil test("p0"); // тестирование времени вывода
      coun1 = 0;
      // вывод данных теплицы 1
      indiTepl1();
      // вывод данных теплицы 2
      indiTepl2();
      // вывод данных теплицы 3
      indiTepl3();
      // вывод данных о работе дизельного обогревателя
      indiGas();
    }
    else if (pageNextion == "p1_0")
    {
      pageNextion_p1(0);
      // TimingUtil test("p1_0"); //тестирование времени вывода
    }
    else if (pageNextion == "p1_1")
      pageNextion_p1(1);
    else if (pageNextion == "p1_2")
      pageNextion_p1(2);
    else if (pageNextion == "p2")
      pageNextion_p2();
    else if (pageNextion == "p3")
      pageNextion_p3();
    digitalWrite(GPIO_NUM_2, !digitalRead(GPIO_NUM_2));
    // vPrintString("sendNextion");
    // vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TIME_UPDATE_HMI));
    vTaskDelay(pdMS_TO_TICKS(TIME_UPDATE_HMI));
  }
  vTaskDelete(NULL);
}

// получение данных от датчиков и управление блоками реле
void updateMB(void *pvParameters)
{
  for (;;)
  {
    for (Sensor_WB_v_3 *_sensor : sensors)
    {
      if (_sensor->getAdress())
      {
        mb_master.readIreg(_sensor->getAdress(), 0, sensor, 3, cbRead);
        update_mbmaster();
        if (!sensor[Sensor_Modbus::mberror])
        {
          _sensor->setTemperature(static_cast<int>(sensor[Sensor_WB_v_3::Sensor_Data::temperature]) * 10.0);
          _sensor->setHumidity(sensor[Sensor_WB_v_3::Sensor_Data::humidity] * 10.0);
          // Serial.printf("Температура %d - %d\n", sensorSM200->getAdress(), sensor[Sensor::SensorSM200::temperature]);
          _sensor->setStatus(Sensor_WB_v_3::NO_ERROR);
        }
        else
        {
          _sensor->setStatus(sensor[Sensor_Modbus::mberror]);
        }
      }
    }
    mb11016p.write();
    soil1.read();
    mbsl8di8ro.write();

    vTaskDelay(pdMS_TO_TICKS(TIME_UPDATE_MODBUS));
  }
  vTaskDelete(NULL);
}

// парсинг полученых данных от дисплея Nextion
// Event Occurs when response comes from HMI
void onHMIEvent(String messege, String data, String response)
{

  if (messege == "page0") //
  {
    pageNextion = "p0";
    return;
  }
  else if (messege == "pageSet") //
  {
    if (data.toInt() == Tepl1.getId())
      pageNextion = "p1_0";
    if (data.toInt() == Tepl2.getId())
      pageNextion = "p1_1";
    if (data.toInt() == Tepl3.getId())
      pageNextion = "p1_2";
    return;
  }
  else if (messege == "page2") //
  {
    pageNextion = "p2";
    return;
  }
  else if (messege == "page3") //
  {
    pageNextion = "p3";
    return;
  }

  else if (messege == "m") //  переключение режим теплица - автомат / ручной
  {
    arr_Tepl[data.toInt() - 1]->setMode(arr_Tepl[data.toInt() - 1]->getMode() == Teplica::AUTO ? Teplica::MANUAL : Teplica::AUTO);
    return;
  }

  else if (messege == "w") //  переключение режим теплица - проветривание
  {
    arr_Tepl[data.toInt() - 1]->setMode(Teplica::AIR);
    return;
  }

  else if (messege == "dh") //  переключение режим теплиц - осушение
  {
    arr_Tepl[data.toInt() - 1]->setMode(Teplica::DECREASE_IN_HUMIDITY);
    return;
  }

  else if (messege == "pump") //
  {
    arr_Tepl[data.toInt() - 1]->setPump(!arr_Tepl[data.toInt() - 1]->getPump());
    return;
  }

  // тестирование работы задвижек
  else if (messege == "tval1") //
  {
    heat.setTestRelay(heat.getValve1(), heat.getStatusRelay(heat.getValve1()) ? OFF : ON);
    return;
  }
  else if (messege == "tval2") //
  {
    heat.setTestRelay(heat.getValve2(), heat.getStatusRelay(heat.getValve2()) ? OFF : ON);
    return;
  }
  else if (messege == "tval3") //
  {
    heat.setTestRelay(heat.getValve3(), heat.getStatusRelay(heat.getValve3()) ? OFF : ON);
    return;
  }

  else if (messege == "set") //
  {
    pars_str_set(data);
    return;
  }
  else if (messege == "adr") //
  {
    pars_str_adr(data);
    return;
  }
  else if (messege == "ss") // изменение скорости шины связи с терминалом
  {
    flash.putInt("sspeed", data.toInt());
    MbSlaveSerial.end();
    MbSlaveSerial.begin(data.toInt()); // Modbus Slave
    return;
  }
  else if (messege == "ms") // изменение скорости шины связи с контроллером реле и контроллером датчиков
  {
    flash.putInt("mspeed", data.toInt());
    MbMasterSerial.end();
    MbMasterSerial.begin(data.toInt(), SERIAL_8N1, RXDMASTER, TXDMASTER, false); // Modbus Master
    return;
  }
  else if (messege == "heat_adr") // изменение адреса контроллера газового нагревателя
  {
    flash.putInt("heat_adr", data.toInt());
    mbsl8di8ro.setAdress(data.toInt());
    return;
  }
}

void pars_str_set(String &str)
{
  int j = 0;
  String st = "";
  for (int i = 0; i < str.length(); i++)
  {
    if (str.charAt(i) == '.')
    {
      arr_set[j] = st.toInt();
      // Serial.println(arr_set[j]);
      j++;
      st = "";
    }
    else
      st += str.charAt(i);
  }
  arr_Tepl[arr_set[0] - 1]->setSetPump(arr_set[1] * 10);
  flash.putUInt(String("SetPump" + String(arr_set[0])).c_str(), arr_set[1] * 10);
  arr_Tepl[arr_set[0] - 1]->setSetHeat(arr_set[2] * 10);
  flash.putUInt(String("SetHeat" + String(arr_set[0])).c_str(), arr_set[2] * 10);

  arr_Tepl[arr_set[0] - 1]->setSetWindow(arr_set[3] * 10);
  flash.putUInt(String("SetSetWindow" + String(arr_set[0])).c_str(), arr_set[3] * 10);

  arr_Tepl[arr_set[0] - 1]->setWindowlevel(arr_set[4]);
  arr_Tepl[arr_set[0] - 1]->setHysteresis(arr_set[5]);
  flash.putUInt(String("Hyster" + String(arr_set[0])).c_str(), arr_set[5]);

  arr_Tepl[arr_set[0] - 1]->setOpenTimeWindow(arr_set[6]);
  flash.putUInt(String("Opentwin" + String(arr_set[0])).c_str(), arr_set[6]);
}

void pars_str_adr(String &str)
{
  
  int j = 0;
  String st = "";
  if (str.isEmpty())
    return;
      
  flash.putString("adr", str);
  
  for (int i = 0; i < str.length(); i++)
  {
    if (str.charAt(i) == '.')
    {
      arr_adr[j] = st.toInt();
      Serial.println(arr_adr[j]);
      j++;
      st = "";
    }
    else
      st += str.charAt(i);
  }
  Tepl1.setAdress(arr_adr[0]);
  hmi("p2.n0.val", arr_adr[0]);
  Tepl1.setCorrectionTemp(10 * arr_adr[1]);
  hmi("p2.t0.txt", String(arr_adr[1]));
  Tepl2.setAdress(arr_adr[2]);
  hmi("p2.n1.val", arr_adr[2]);
  Tepl2.setCorrectionTemp(10 * arr_adr[3]);
  hmi("p2.t1.txt", String(arr_adr[3]));
  Tepl3.setAdress(arr_adr[4]);
  hmi("p2.n2.val", arr_adr[4]);
  Tepl3.setCorrectionTemp(10 * arr_adr[5]);
  hmi("p2.t2.txt", String(arr_adr[5]));
  hmi("p2.n4.val", arr_adr[10]);
  mb11016p.setAdress(arr_adr[11]);
  hmi("p2.n5.val", arr_adr[11]);
}

// заполнение таблицы для передачи (протокол Modbus)
void saveOutModBusArr()
{
  // для терминала
  slave.Hreg(rs485mode1, Tepl1.getMode() | Tepl1.getPump() << 2 | Tepl1.getHeat() << 3 | Tepl1.getSetPump() / 10 << 8);
  slave.Hreg(rs485mode11, Tepl1.getLevel() | (Tepl1.getSetWindow() - Tepl1.getSetPump()) / 100 << 8);
  slave.Hreg(rs485mode2, Tepl2.getMode() | Tepl2.getPump() << 2 | Tepl2.getHeat() << 3 | Tepl2.getSetPump() / 10 << 8);
  slave.Hreg(rs485mode21, Tepl2.getLevel() | (Tepl2.getSetWindow() - Tepl2.getSetPump()) / 100 << 8);
  slave.Hreg(rs485mode3, Tepl3.getMode() | Tepl3.getPump() << 2 | Tepl3.getHeat() << 3 | Tepl3.getSetPump() / 10 << 8);
  slave.Hreg(rs485mode31, Tepl3.getLevel() | (Tepl3.getSetWindow() - Tepl3.getSetPump()) / 100 << 8);
  //  slave.Hreg(rs485mode7, Tepl_.getMode() | Tepl_.getPump() << 2 | Tepl_.getHeat() << 3 | Tepl_.getSetPump() / 10 << 8);
  //  slave.Hreg(rs485mode71, Tepl_.getLevel() | (Tepl_.getSetWindow() - Tepl_.getSetPump()) / 100 << 8);
  slave.Hreg(rs485temperature1, Tepl1.getTemperature() / 10);
  slave.Hreg(rs485temperature2, Tepl2.getTemperature() / 10);
  slave.Hreg(rs485temperature3, Tepl3.getTemperature() / 10);

  slave.Hreg(rs485error45, Tepl1.getSensorStatus() | Tepl2.getSensorStatus() << 8);
  slave.Hreg(rs485error67, Tepl3.getSensorStatus());

  // для SCADA
  int i = 0;
  for (Teplica *t : arr_Tepl)
  {
    slaveWiFi.Ireg(WiFimode1 + i, t->getMode());
    slaveWiFi.Ireg(WiFipump1 + i, t->getPump());
    slaveWiFi.Ireg(WiFiheat1 + i, t->getHeat());
    slaveWiFi.Ireg(WiFisetpump1 + i, t->getSetPump());
    slaveWiFi.Ireg(WiFitemperature1 + i, t->getTemperature());
    
    slaveWiFi.Ireg(WiFihumidity1 + i, t->getHumidity());

    slaveWiFi.Ireg(WiFierror1 + i, t->getSensorStatus());
    slaveWiFi.Ireg(WiFiLevel1 + i, t->getLevel());
    slaveWiFi.Ireg(WiFisetwindow1 + i, t->getSetWindow());
    slaveWiFi.Ireg(WiFisetheat1 + i, t->getSetHeat());
    slaveWiFi.Ireg(WiFiHysteresis1 + i, t->getHysteresis());
    slaveWiFi.Ireg(WiFiOpenTimeWindow1 + i, t->getOpenTimeWindow());
    i += 17;
  }
  slaveWiFi.Ireg(WiFiSoilSensorT2, soil1.getTemperature());
  slaveWiFi.Ireg(WiFiSoilSensorH2, soil1.getHumidity());
  slaveWiFi.Ireg(WiFiSoilSensorC2, soil1.getConductivity());
  slaveWiFi.Ireg(WiFiSoilSensorS2, soil1.getSalinity());
  slaveWiFi.Ireg(WiFiSoilSensorTDS2, soil1.getTDS());
}

void update_WiFiConnect(void *pvParameters)
{
  /*----настройка Wi-Fi---------*/
  for (;;)
  {
    int counter_WiFi = 0;
    while (WiFi.status() != WL_CONNECTED && counter_WiFi < 10)
    {
      WiFi.disconnect();
      WiFi.reconnect();
      // Serial.println("Reconecting to WiFi..");
      counter_WiFi++;
      delay(1000);
    }
    if (WiFi.status() == WL_CONNECTED)
      Serial.printf("Connect to:\t%s\n", ssid);
    else
      Serial.printf("Dont connect to: %s\n", ssid);
    vTaskDelay(pdMS_TO_TICKS(10 * 60 * 1000));
  }
  vTaskDelete(NULL);
}

/*--------------------------------------- изменения параметров с компьютера ------------------------------------*/
void controlScada()
{
  int number = 0xfff;

  if (slave.Hreg(wifi_flag_edit_1))
  {
    number = 0;
    slave.Hreg(wifi_flag_edit_1, 0);
  }
  if (slave.Hreg(wifi_flag_edit_2))
  {
    number = 1;
    slave.Hreg(wifi_flag_edit_2, 0);
  }
  if (slave.Hreg(wifi_flag_edit_3))
  {
    number = 2;
    slave.Hreg(wifi_flag_edit_3, 0);
  }
  // if (slave.Hreg(wifi_flag_edit_7))
  // {
  //   number = 3;
  //   slave.Hreg(wifi_flag_edit_7, 0);
  // }
  if (number == 0xfff)
    return;

  int k = (int(wifi_HOLDING_REGS_SIZE) - int(wifi_flag_edit_1)) / 4;
  k *= number;
  arr_Tepl[number]->setSetPump(slave.Hreg(wifi_UstavkaPump_1 + k));
  flash.putUInt(String("SetPump" + String(arr_Tepl[number]->getId())).c_str(), slave.Hreg(wifi_UstavkaPump_1 + k));

  arr_Tepl[number]->setSetHeat(slave.Hreg(wifi_UstavkaHeat_1 + k));
  flash.putUInt(String("SetHeat" + String(arr_Tepl[number]->getId())).c_str(), slave.Hreg(wifi_UstavkaHeat_1 + k));

  arr_Tepl[number]->setSetWindow(slave.Hreg(wifi_UstavkaWin_1 + k));
  flash.putUInt(String("SetSetWindow" + String(arr_Tepl[number]->getId())).c_str(), slave.Hreg(wifi_UstavkaWin_1 + k));

  arr_Tepl[number]->setMode(slave.Hreg(wifi_mode_1 + k));

  arr_Tepl[number]->setMode(slave.Hreg(wifi_mode_1 + k));
  if (arr_Tepl[number]->getMode() == Teplica::MANUAL)
  {
    arr_Tepl[number]->setPump(slave.Hreg(wifi_pump_1 + k));
    arr_Tepl[number]->setHeat(slave.Hreg(wifi_heat_1 + k));
    arr_Tepl[number]->setWindowlevel(slave.Hreg(wifi_setWindow_1 + k));
  }
  if (arr_Tepl[number]->getMode() == Teplica::AIR)
    arr_Tepl[number]->air(AIRTIME, 0);

  if (arr_Tepl[number]->getMode() == Teplica::DECREASE_IN_HUMIDITY)
    arr_Tepl[number]->decrease_in_humidity(AIRTIME, 0);

  arr_Tepl[number]->setHysteresis(slave.Hreg(wifi_hysteresis_1 + k));
  flash.putUInt(String("Hyster" + String(arr_Tepl[number]->getId())).c_str(), slave.Hreg(wifi_hysteresis_1 + k));

  arr_Tepl[number]->setOpenTimeWindow(slave.Hreg(wifi_time_open_windows_1 + k));
  flash.putUInt(String("Opentwin" + String(arr_Tepl[number]->getId())).c_str(), slave.Hreg(wifi_time_open_windows_1 + k));
}

String calculateTimeWork()
{
  String str = "\nDuration of work: ";
  unsigned long currentMillis = millis();
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  currentMillis %= 1000;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;

  if (days > 0)
  {
    str += String(days) + " d ";
    str += String(hours) + " h ";
    str += String(minutes) + " min ";
  }
  else if (hours > 0)
  {
    str += String(hours) + " h ";
    str += String(minutes) + " min ";
  }
  else if (minutes > 0)
    str += String(minutes) + " min ";
  str += String(seconds) + " sec";
  return str;
}

// регулировка окон
void updateGreenHouse(void *pvParameters)
{
  for (;;)
  {
    for (Teplica *t : arr_Tepl)
    {
      int level_window_tmp = t->getLevel();

      if (t->getSensorStatus() == Teplica::NO_ERROR && t->getThereAreWindows())
      {
        t->regulationWindow(t->getTemperature());
      }
      if (level_window_tmp != t->getLevel())
      {
        flash.putUInt(String("LevelWindow" + String(t->getId())).c_str(), t->getLevel()); // запись уровня открытия окна при его изменении.
        // Serial.printf("\nТеплица %d : %d", t->getId(), t->getLevel());
      }
    }
    vTaskDelay(TIME_UPDATE_GREENOOUSE * 60 * 1000 / portTICK_PERIOD_MS);
  }
}

// вывод данных теплицы 1
void indiTepl1()
{
  hmi.inditepl1(Tepl1, heat);
}

// вывод данных теплицы 2
void indiTepl2()
{
  hmi.inditepl2(Tepl2, heat);
}

// вывод данных теплицы 3
void indiTepl3()
{
  hmi.inditepl3(Tepl3, heat);
}

// вывод данных о работе дизельного обогревателя
void indiGas()
{
  // идикация состояния компрессора
  hmi("gm0.en", heat.getSatusHeat() ? 1 : 0);
  // идикация состояния задвижек
  hmi("p8.pic", heat.getStatusRelay(heat.getValve1()) ? 12 : 11);
  hmi("p9.pic", heat.getStatusRelay(heat.getValve2()) ? 12 : 11);
  hmi("p10.pic", heat.getStatusRelay(heat.getValve3()) ? 12 : 11);
}

// окно установок теплиц
void pageNextion_p1(int i)
{
  String err = "";
  if (mb11016p.getError())
    err = "MB16R: " + String(mb11016p.getError());
  if (mbsl8di8ro.getError())
    err += " MBSL8di8ro: " + String(mbsl8di8ro.getError());
  if (!err.length())
    err = "Mb adress: " + String(IDSLAVE) + " | " + "\nRSSI: " + String(WiFi.RSSI()) + " | " + WiFi.localIP().toString();

  coun1++;
  hmi.hmi_p1(*arr_Tepl[i], err, coun1);
}

void pageNextion_p2()
{
  if (coun1 < 5)
  {
    String incStr = flash.getString("adr", "");
    pars_str_adr(incStr);

    switch (flash.getInt("sspeed", 2400))
    {
    case 2400:
      hmi("r4.val=1");
      break;
    case 9600:
      hmi("r3.val=1");
      break;
    case 19200:
      hmi("r5.val=1");
      break;
    }
    switch (flash.getInt("mspeed", 2400))
    {
    case 2400:
      hmi("r0.val=1");
      break;
    case 9600:
      hmi("r1.val=1");
      break;
    case 19200:
      hmi("r2.val=1");
      break;
    }
    coun1++;
  }
}

// вывод данных о работе дизельного обогревателя
void pageNextion_p3()
{
  if (coun1 < 5)
  {
    hmi("p3.n0.val", flash.getInt("heat_adr", 102));
    coun1++;
  }
  indiGas();
}

void update_mbmaster()
{
  unsigned long t = millis() + 420;
  while (t > millis())
  {
    mb_master.task();
  }
}

void wifiInit()
{
  EEPROM.begin(100);
  EEPROM.get(0, lp);

  // пытаемся подключиться
  Serial.print("Connect to: ");
  Serial.println(ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED && millis() < 30000)
  {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println();
    Serial.print("Connected! Local IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    loginPortal();
  }
}

void loginPortal()
{
  Serial.println("\nPortal start");
  // String res{};
  // {
  //   WiFi.disconnect();
  //   int counterWiFi = WiFi.scanNetworks();
  //   for (auto i = 0; i < counterWiFi; i++)
  //   {
  //     res += WiFi.SSID(i).c_str();
  //     res += ',';
  //   }
  // }
   WiFi.mode(WIFI_AP);
    // WiFi.softAP("Tepl1_3");
    WiFi.begin("Tepl_1-3", password.c_str());
    Serial.println(WiFi.softAPIP()); // по умолч. 192.168.4.1
  // запускаем портал
  GyverPortal ui;
  ui.attachBuild(buildLoginPage);
  ui.start();
  ui.attach(action);
  // работа портала
  while (ui.tick())
  {
    if (millis() > 40000)
      return;
  }
  Serial.println();
  Serial.println("Exit portal");
}

void buildLoginPage()
{
  GP.BUILD_BEGIN();
  GP.THEME(GP_DARK);
  GP.FORM_BEGIN("/login");
  GP.TEXT("lg", "Login", lp.ssid);
  GP.BREAK();
  GP.PASS("ps", "Password", lp.pass);
  GP.SUBMIT("Submit");
  GP.FORM_END();
  GP.BUILD_END();
}

void buildLoginPage(String wifi)
{
  int wifinumder = 0;
  GP.BUILD_BEGIN();
  GP.THEME(GP_DARK);
  GP.FORM_BEGIN("/login");
  GP.SELECT("lg", wifi, wifinumder);
  for (auto i = 0; i < 20; i++)
  {
    lp.ssid[i] = 0;
  }
  for (auto i = 0; i < WiFi.SSID(wifinumder).length(); i++)
  {
    lp.ssid[i] = WiFi.SSID(wifinumder)[i];
  }
  GP.BREAK();
  GP.PASS("ps", "Password", lp.pass);
  GP.SUBMIT("Submit");
  GP.FORM_END();
  GP.BUILD_END();
}

void action(GyverPortal &p)
{
  if (p.form("/login"))
  {                           // кнопка нажата
    p.copyStr("lg", lp.ssid); // копируем себе
    p.copyStr("ps", lp.pass);
    EEPROM.put(0, lp);       // сохраняем
    EEPROM.commit();         // записываем
    WiFi.softAPdisconnect(); // отключаем AP
  }
}

void buildPage()
{
  GP.BUILD_BEGIN(1200);
  GP.THEME(GP_DARK);
  GP.PAGE_TITLE("Теплицы 1-3");
  GP.UPDATE("t1,h1,s1,t2,h2,s2,t3,h3,s3,win1,win2,win3,uh1,up1,ut1,uh2,up2,ut2,uh3,up3,ut3,pump1,pump2,pump3,mode1,mode2,mode3");
  // позволяет "отключить" таблицу при ширине экрана меньше 700px
  GP.GRID_RESPONSIVE(600);
  GP.NAV_TABS("Мониторинг,Настройка");
  GP.FORM_BEGIN("/seting");

  GP.NAV_BLOCK_BEGIN();
  M_GRID(
      M_BLOCK_TAB(
          "Теплица 1", "", GP_GRAY_B,
          M_BOX(GP.LABEL("Т, °C"); GP.TEXT("t1", String(Tepl1.getTemperature() / 100.0, 1), "", "75px"); GP.LABEL("H, %"); GP.TEXT("h1", String(Tepl1.getHumidity() / 100), "", "75px"););
          M_BOX(GP.LABEL("Авария"); GP.LED_RED("s1", 0););
          M_BOX(GP.LABEL("Окно"); GP.SLIDER("win1", 0, 0, 100, 1, 0, "", 0, 0););
          M_BOX(GP.TEXT("uh1", String(Tepl1.getSetHeat() / 100.0, 1), "", "75px"); GP.TEXT("up1", String(Tepl1.getSetPump() / 100.0, 1), "", "75px");GP.TEXT("ut1", String(Tepl1.getSetWindow() / 100.0, 1), "", "75px"););
          M_BOX(GP.LABEL("Режим"); GP.TEXT("mode1", "AUTO"); GP.LABEL("Насос"); GP.LED_GREEN("pump1", 0);););
      M_BLOCK_TAB(
          "Теплица 2", "", GP_GRAY_B,
          M_BOX(GP.LABEL("Т, °C"); GP.TEXT("t2", String(Tepl2.getTemperature() / 100.0, 1), "", "75px"); GP.LABEL("H, %"); GP.TEXT("h2", String(Tepl2.getHumidity() / 100), "", "75px"););
          M_BOX(GP.LABEL("Авария"); GP.LED_RED("s2", 0););
          M_BOX(GP.LABEL("Окно"); GP.SLIDER("win2", 0, 0, 100, 1, 0, "", 1, 0););
          M_BOX(GP.TEXT("uh2", String(Tepl2.getSetHeat() / 100.0, 1), "", "75px"); GP.TEXT("up2", String(Tepl2.getSetPump() / 100.0, 1), "", "75px");GP.TEXT("ut2", String(Tepl2.getSetWindow() / 100.0, 1), "", "75px"););
          M_BOX(GP.LABEL("Режим"); GP.TEXT("mode2", "AUTO"); GP.LABEL("Насос"); GP.LED_GREEN("pump2", 0);););
      M_BLOCK_TAB(
          "Теплица 3", "", GP_GRAY_B,
          M_BOX(GP.LABEL("Т, °C"); GP.TEXT("t3", String(Tepl3.getTemperature() / 100.0, 1), "", "75px"); GP.LABEL("H, %"); GP.TEXT("h3", String(Tepl3.getHumidity() / 100), "", "75px"););
          M_BOX(GP.LABEL("Авария"); GP.LED_RED("s3", 0););
          M_BOX(GP.LABEL("Окно"); GP.SLIDER("win3", 0, 0, 100, 1, 0, "", 1, 0););
          M_BOX(GP.TEXT("uh3", String(Tepl3.getSetHeat() / 100.0, 1), "", "75px"); GP.TEXT("up3", String(Tepl3.getSetPump() / 100.0, 1), "", "75px");GP.TEXT("ut3", String(Tepl3.getSetWindow() / 100.0, 1), "", "75px"););
          M_BOX(GP.LABEL("Режим"); GP.TEXT("mode3", "AUTO"); GP.LABEL("Насос"); GP.LED_GREEN("pump3", 0););););
  GP.NAV_BLOCK_END();
  GP.NAV_BLOCK_BEGIN();
  M_GRID(
      M_BLOCK_TAB(
          "Теплица 1", "", GP_GRAY_B,
          M_BOX(GP.LABEL("Адрес:"); GP.SPINNER("adr1", Tepl1.getAdressT(), 1, 250););
          M_BOX(GP.LABEL("Кор T, °C:"); GP.SPINNER("corT1", Tepl1.getCorrectionTemp() / 100.0, -2.0, 2.0, 0.1, 1);););
      M_BLOCK_TAB(
          "Теплица 2", "", GP_GRAY_B,
          M_BOX(GP.LABEL("Адрес:"); GP.SPINNER("adr2", Tepl2.getAdressT(), 1, 250););
          M_BOX(GP.LABEL("Кор T, °C:"); GP.SPINNER("corT2", Tepl2.getCorrectionTemp() / 100.0, -2.0, 2.0, 0.1, 1);););
      M_BLOCK_TAB(
          "Теплица 3", "", GP_GRAY_B,
          M_BOX(GP.LABEL("Адрес:"); GP.SPINNER("adr3", Tepl3.getAdressT(), 1, 250););
          M_BOX(GP.LABEL("Кор T, °C:"); GP.SPINNER("corT3", Tepl3.getCorrectionTemp() / 100.0, -2.0, 2.0, 0.1, 1););););
  
  int sp{};
  switch (flash.getInt("mspeed", 2400))
  {
  case 2400:
   sp = 1;
    break;
  case 9600:
   sp = 2;
    break;
  case 19200:
   sp = 3;
    break;
  
  default:
    break;
  }
  M_BOX(GP.LABEL("Скорость датчиков (1-2400, 2-9600, 3-19200)"); GP.SPINNER("ms", sp, 1, 3); GP.SUBMIT("Применить"););
  GP.NAV_BLOCK_END();
  GP.FORM_END();

  GP.AJAX_PLOT_DARK("plot", names, 3, 1800, 1800, 300);
  GP.BUILD_END();
}

void actionPage()
{

  if (ui.update("plot"))
  {
    int answ[] = {round(Tepl1.getTemperature() / 100.0),
                  round(Tepl2.getTemperature() / 100.0),
                  round(Tepl3.getTemperature() / 100.0)};
    ui.answer(answ, 3);
  }

  if (ui.update())
  {
    String t{};
    String h{};
    bool st = true;

    if (Tepl1.getSensorStatus() == Sensor_WB_v_3::NO_ERROR)
    {
      t = String(Tepl1.getTemperature() / 100.0, 1);
      h = String(Tepl1.getHumidity() / 100);
      st = false;
    }
    else
    {
      st = true;
      t = "--";
      h = "--";
    }
    ui.updateString("t1", t);
    ui.updateString("h1", h);
    ui.updateBool("s1", st);
    t = String(Tepl1.getSetHeat() / 100.0, 1);
    ui.updateString("uh1", t);
    t = String(Tepl1.getSetPump() / 100.0, 1);
    ui.updateString("up1", t);
    t = String(Tepl1.getSetWindow() / 100.0, 1);
    ui.updateString("ut1", t);
    ui.updateInt("win1", Tepl1.getLevel());
    ui.updateBool("pump1", Tepl1.getPump());
    int mod = Tepl1.getMode();
    String str_mode{};
    switch (mod)
    {
    case 0:
      str_mode = "АВТО";
      break;
    case 1:
      str_mode = "РУЧНОЙ";
      break;
    case 2:
      str_mode = "ПРОВЕТРИВАНИЕ";
      break;
    case 3:
      str_mode = "ОСУШЕНИЕ";
      break;
    
    default:
      break;
    }
    ui.updateString("mode1", str_mode);

    if (Tepl2.getSensorStatus() == Sensor_WB_v_3::NO_ERROR)
    {
      t = String(Tepl2.getTemperature() / 100.0, 1);
      h = String(Tepl2.getHumidity() / 100);
      st = false;
    }
    else
    {
      st = true;
      t = "--";
      h = "--";
    }
    ui.updateString("t2", t);
    ui.updateString("h2", h);
    ui.updateBool("s2", st);

    if (Tepl3.getSensorStatus() == Sensor_WB_v_3::NO_ERROR)
    {
      t = String(Tepl3.getTemperature() / 100.0, 1);
      h = String(Tepl3.getHumidity() / 100);
      st = false;
    }
    else
    {
      st = true;
      t = "--";
      h = "--";
    }
    ui.updateString("t3", t);
    ui.updateString("h3", h);
    ui.updateBool("s3", st);
  }

  if (ui.form("/seting"))
  {
      String set{};
      int answ{};
      float t{};

      ui.copyInt("adr1", answ);
      set += String(answ) + ".";
      ui.copyFloat("corT1", t);
      set += String(t * 10, 0) + ".";

      ui.copyInt("adr2", answ);
      set += String(answ) + ".";
      ui.copyFloat("corT2", t);
      set += String(t * 10, 0) + ".";
  
      ui.copyInt("adr3", answ);
      set += String(answ) + ".";
      ui.copyFloat("corT3", t);
      set += String(t * 10, 0) + ".";

      pars_str_adr(set);
      int speed{};
      ui.copyInt("ms", answ);
      switch (answ)
      {
      case 1:
        speed = 2400;
        break;
      case 2:
        speed = 9600;
        break;
      case 3:
        speed = 19200;
        break;
      
      default:
        break;
      }

      flash.putInt("mspeed", speed);
      MbMasterSerial.end();
      MbMasterSerial.begin(speed, SERIAL_8N1, RXDMASTER, TXDMASTER, false); // Modbus Master
      Serial.println(set);
  }  
}