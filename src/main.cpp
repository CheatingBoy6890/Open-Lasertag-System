#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <IRsend.h>
#include <NeoPixelBus.h>

#include <painlessMesh.h>

#include <SPIFFS.h>

#include <Wire.h>
#include <U8g2lib.h>

#include "XT_DAC_Audio.h"
#include "sound.h"

#define LED_PIN 19
#define NUMLEDS 4

#define SHOOT_BUTTON 2
#define LASER_BUTTON 17
#define RED_LASER 16

#define SHOOT_DELAY 300
#define RCV_PIN 27
#define TRX_PIN 13
#define TEAM_SelTime 5000
#define Down_Time 1000
#define VEST_CONNECTTIME 7000

#define MESH_SSID "Lasertag"
#define MESH_PASSWORD "PWA_Lasertag"
#define MESH_PORT 5555

int TeamId = 0;
bool alive = false;
int playerId = 0;
int mypoints = 0;
uint32_t bound_vest;

int brightness = 64; // must be an even number

uint8_t Y = 15;

bool isRunning = false;

RgbColor Team[4] = {
    RgbColor(brightness, 0, 0),
    RgbColor(0, 0, brightness),
    RgbColor(brightness / 2, brightness / 2, 0),
    RgbColor(0, brightness, 0),
};

NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> strip(NUMLEDS, LED_PIN);

U8G2_SH1106_128X64_NONAME_F_HW_I2C Display(U8G2_R0, /* clock=*/SCL, /* data=*/SDA, /* reset=*/U8X8_PIN_NONE);

XT_Wav_Class Shoot_sound(shoot_sound);
XT_DAC_Audio_Class DacAudio(25, 0);

/*
Led 0 : alive or not
Led 1 : Team
Led 3 : Shooting
Led 4 : Bluetooth (todo)
*/
Scheduler userScheduler;
painlessMesh Lasermesh;
void changedConnectionCallback();
void OnNewConnection(uint32_t nodeId);
void receivedCallback(uint32_t from, String &msg);
void oledPrint(String s);

SimpleList<uint32_t> nodes;
uint32_t *node_array;
int size;

void decode();

void red_laser();

// Task taskLoopAudio(TASK_MILLISECOND * 50,TASK_FOREVER,&LoopAudio);
Task taskDecode(TASK_MILLISECOND * 70, TASK_FOREVER, &decode, &userScheduler, true);

IRsend sender(TRX_PIN);

IRrecv receiver(RCV_PIN);
decode_results results;

TaskHandle_t shoot_task_handle, regen_task_handle;

void Fill_Strip(uint16_t from, uint16_t to, RgbColor color);
void Connect_Vest();

void sendMilesTag(uint32_t player, uint32_t team, uint32_t dammage)
{
  uint64_t data = 0;
  data = player;
  data = data << 2;
  data = data + team;
  data = data << 4;
  data = data + dammage;
  sender.sendMilestag2(data);
}

void shoot(void *param)
{

  while (true)
  {
    vTaskSuspend(NULL);

    sendMilesTag(playerId, TeamId, 15);
    DacAudio.Play(&Shoot_sound);

    // sendMilesTag(0, TeamId, 15);

    Serial.println("Peng!");

    strip.SetPixelColor(2, RgbColor(brightness));
    strip.Show();
    vTaskDelay(15);
    strip.SetPixelColor(2, RgbColor(0));
    strip.Show();
    vTaskDelay(SHOOT_DELAY - 15);

    digitalWrite(25, LOW);
  }
}

void onshoot()
{
  if (!isRunning)
  {
    Lasermesh.sendBroadcast("Start_Game", true);
  }

  if (alive)
  {
    vTaskResume(shoot_task_handle);
  }
  // vTaskDelay(SHOOT_DELAY);
}

void regenerate(void *param)
{

  while (true)
  {
    vTaskSuspend(NULL);
    vTaskDelay(Down_Time);
    alive = true;
    Lasermesh.sendSingle(bound_vest, "alive");
    strip.SetPixelColor(0, RgbColor(0, brightness, 0));
    strip.Show();
  }
}

void decode()
{

  // Serial.println("Trying to decode");

  if (receiver.decode(&results))
  {

    Serial.println("#################");
    Serial.print("Decode Type:");
    Serial.println(results.decode_type);

    Serial.print("Team:");
    Serial.println(results.command >> 4);

    Serial.print("Player:");
    Serial.println(results.address);

    Serial.print("Dammage:");
    Serial.println(results.command & 0x30);

    oledPrint(String("Message!"));
    oledPrint(String("T:") + (results.command >> 4) + " P:" + results.address + String(" D:") + (results.command & 0x30) + " Type:" + results.decode_type);

    if (results.decode_type == 97)
    {
      alive = false;
      vTaskResume(regen_task_handle);
      strip.SetPixelColor(0, RgbColor(brightness, 0, 0));
      strip.Show();

      // serialPrintUint64(results.value, HEX);
      Serial.println("");
    }
    receiver.resume(); // Receive the next value
    // delay(2000);
    // Serial.println("End of the loop");
  }
}

void teamselect()
{
  oledPrint("Starting Team selection");

  unsigned long seltime = millis() + TEAM_SelTime;
  Fill_Strip(0, NUMLEDS, Team[TeamId]);
  strip.Show();
  while (seltime > millis())
  {
    if (digitalRead(SHOOT_BUTTON) == LOW)
    {
      TeamId = TeamId + 1;
      TeamId = TeamId % 4;
      Fill_Strip(0, NUMLEDS, Team[TeamId]);
      strip.Show();
      Serial.print("Team: ");
      Serial.println(TeamId);

      oledPrint(String("Team: ") + TeamId);
    }
    vTaskDelay(30);
  }
  // alive = true;
  Fill_Strip(0, NUMLEDS, 0);
  strip.SetPixelColor(1, Team[TeamId]);
  Serial.print("finished Teamselection, Team:");
  Serial.println(TeamId);

  oledPrint("finished selection");
}

/*####################################################
  /$$$$$$              /$$
 /$$__  $$            | $$
| $$  \__/  /$$$$$$  /$$$$$$   /$$   /$$  /$$$$$$
|  $$$$$$  /$$__  $$|_  $$_/  | $$  | $$ /$$__  $$
 \____  $$| $$$$$$$$  | $$    | $$  | $$| $$  \ $$
 /$$  \ $$| $$_____/  | $$ /$$| $$  | $$| $$  | $$
|  $$$$$$/|  $$$$$$$  |  $$$$/|  $$$$$$/| $$$$$$$/
 \______/  \_______/   \___/   \______/ | $$____/
                                        | $$
                                        | $$
                                        |__/
#####################################################*/

void setup()
{
  Serial.begin(115200);
  Display.begin();
  Serial.println("Setup");
  Display.setFont(u8g2_font_ncenB08_tr); // choose a suitable font
  // Display.drawStr(0, 10, "Hello World!"); // write something to the internal memory
  oledPrint("Running Setup");

  SPIFFS.begin();

  strip.Begin();
  strip.Show();

  pinMode(SHOOT_BUTTON, INPUT_PULLUP);
  pinMode(LASER_BUTTON, INPUT_PULLUP);
  pinMode(RED_LASER, OUTPUT);

  Lasermesh.setDebugMsgTypes(ERROR | DEBUG | STARTUP); // set before init() so that you can see error messages
  Lasermesh.init(MESH_SSID, MESH_PASSWORD, &userScheduler, MESH_PORT);
  Lasermesh.onChangedConnections(&changedConnectionCallback);
  Lasermesh.onNewConnection(&OnNewConnection);
  Lasermesh.onReceive(&receivedCallback);

  receiver.enableIRIn();
  sender.begin();

  xTaskCreate(
      shoot,
      "shoot",
      2048,
      NULL,
      1,
      &shoot_task_handle);

  xTaskCreate(
      regenerate,
      "regen",
      2048,
      NULL,
      1,
      &regen_task_handle);

  teamselect();
  Connect_Vest();
  attachInterrupt(SHOOT_BUTTON, onshoot, FALLING);
  attachInterrupt(LASER_BUTTON, red_laser, CHANGE);

  // strip.SetPixelColor(0, Team[1]);
  // strip.Show();
}

void loop()
{

  Lasermesh.update();
  // Serial.println("Loop the loop");
  // vTaskDelay(2000);
  // // sender.sendLasertag(0x643554,0,1);
  // sendMilesTag(3,2,6);
  // Serial.println("Shooting!");
  if (Shoot_sound.Playing)
  {
    DacAudio.FillBuffer();
  }
}

void OnNewConnection(uint32_t nodeId)
{

  Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
  Serial.printf("--> startHere: New Connection, %s\n", Lasermesh.subConnectionJson(true).c_str());

  oledPrint(String("New Connection:") + nodeId);
}

void Start_Game()
{
}

void changedConnectionCallback()
{
  Serial.printf("Changed connections\n");

  nodes = Lasermesh.getNodeList(true);
  size = nodes.size();

  Serial.printf("Num nodes: %d\n", size);
  Serial.printf("Connection list:");

  SimpleList<uint32_t>::iterator node = nodes.begin();
  while (node != nodes.end())
  {
    Serial.printf(" %u", *node);
    node++;
  }
  Serial.println();
  if (!isRunning)
  {
    // Sort

    Serial.println("Sorting the node list to use as player IDs");
    // oledPrint("Sorting List!");

    int k = 0;
    node_array = new uint32_t[size];
    for (int const &i : nodes)
    {
      node_array[k++] = i;
    }
    std::sort(node_array, node_array + size);

    int i = 0;
    while (i < size)
    {
      if (node_array[i] == Lasermesh.getNodeId())
      {
        playerId = i;
        break;
      }
      i++;
    }

    Serial.println("Sorting finished!");
  }
}

void receivedCallback(uint32_t from, String &msg)
{
  Serial.printf("startHere: Received from %u msg=%s\n", from, msg.c_str());

  if (msg == "Start_Game")
  {

    Start_Game();
  }
}

void oledPrint(String s)
{
  Y = Y > 64 ? Display.clearBuffer(), 15 : Y;
  Display.drawStr(0, Y, s.c_str());
  Y += 15;
  Display.sendBuffer();
}

void red_laser()
{
  digitalWrite(RED_LASER, digitalRead(LASER_BUTTON));
}

void got_hit(uint32_t player)
{
  Lasermesh.sendSingle(node_array[player], "Got_Hit");
  Lasermesh.sendSingle(bound_vest, "dead");
}

void hit_someone()
{
  mypoints++;
}

void Fill_Strip(uint16_t from, uint16_t to, RgbColor color)
{
  for (from; from < to; from++)
  {
    strip.SetPixelColor(from, color);
  }
}

void Connect_Vest()
{
  uint64_t connect_time = millis() + VEST_CONNECTTIME;
  while (connect_time < millis())
  {
    if (digitalRead(SHOOT_BUTTON) == LOW)
    {
      sendMilesTag(playerId, TeamId, 15);
    }
  }
}
