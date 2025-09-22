#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "alpaca.h"

// This is a dummy "motor" class that has both a "step position" and also a "real number" position
// Steps are using motor reference, while real are for user units (like degree or length)
// Of course this is non functionning and only here to illustrate the Alpaca stuff!
class CMotor { public:
    int pos= 0;
    int maxPos=0;
    int maxSpeed=0;
    int maxAcceleration=0;
    float minRealPos= 0.0f, maxRealPos= 100.0f;
    CMotor(int maxPos, int maxSpeed, int maxAcceleration, float minRealPos, float maxRealPos): maxPos(maxPos), maxSpeed(maxSpeed), maxAcceleration(maxAcceleration), minRealPos(minRealPos), maxRealPos(maxRealPos) { }

    bool isMoving() { return false; }
    void goToSteps(int32_t destination) { pos= destination; } 
    void stop() { } 
    void goToReal(float destination) { pos= int((destination-minRealPos)*maxPos/(maxRealPos-minRealPos)); } 
    float posInReal() { return pos*(maxRealPos-minRealPos)/maxPos+minRealPos; }

    bool isGuiding= false;
    void guide(float rate, int lengthms) {} // move the motor at rate (sign indicate direction) for lengthms miliseconds... (-1 indicate forever)

    int tracking; // indicate trackign speed
};

CMotor MotorFocus(10000, 400, 800, 0.0f, 100.0f); // a motor with 10k steps, top speed of 400 steps per second, acceleration of 800 steps/s² and a range from 0 to 100mm

// Here I define my focusser class...
class CMyFocuser : public CFocuser
{ public:
    CMyFocuser(int id): CFocuser(id, "CdB Focuser Driver", "1", "CdB Alpaca Focuser", "Focuser for eqMount") { }
    // I am overriding "working functions" calling the motor functions to execute....
    bool get_absolute() override { return true; }
    bool get_ismoving() override { return MotorFocus.isMoving(); }
    int32_t get_maxincrement() override { return MotorFocus.maxPos; }
    int32_t get_maxstep() override { return MotorFocus.maxPos; }
    int32_t get_position() override { return MotorFocus.pos; }
    int32_t get_stepsize() override { return int32_t((MotorFocus.maxRealPos-MotorFocus.minRealPos)/MotorFocus.maxPos); }
    TAlpacaErr put_halt() override { MotorFocus.stop(); return ALPACA_OK; };
    TAlpacaErr put_move(int32_t position) override { MotorFocus.goToSteps(position); return ALPACA_OK; };
    // This is the overload of the subLoad to get my init values from storage...
    void subLoad(CAlpaca *alpaca) override
    {
        CFocuser::subLoad(alpaca);
        MotorFocus.maxPos= alpaca->load(keyHeader, "maxPos", int32_t(MotorFocus.maxPos));
        MotorFocus.maxSpeed= alpaca->load(keyHeader, "maxSpeed", int32_t(MotorFocus.maxSpeed));
        MotorFocus.maxAcceleration= alpaca->load(keyHeader, "maxAcceleration", int32_t(MotorFocus.maxAcceleration));
        MotorFocus.maxRealPos= alpaca->load(keyHeader, "maxRealPos", MotorFocus.maxRealPos);
    }
    // My setup web page adds configuration for the focusser settings...
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override
    {
        if (data!=nullptr)
        {   // This handles when the user changes the settings. It saves them to the motor and storage...
            int v= getIntData(data,"FocMax"); if (v!=-1) alpaca->save(keyHeader, "maxPos", int32_t(MotorFocus.maxPos=v));
            v= getIntData(data,"FocMaxSpd");  if (v!=-1) alpaca->save(keyHeader, "maxSpeed", int32_t(MotorFocus.maxSpeed=v));
            v= getIntData(data,"FocAcc");     if (v!=-1) alpaca->save(keyHeader, "maxAcceleration", int32_t(MotorFocus.maxAcceleration=v));
            v= getIntData(data,"FocStep");    if (v!=-1) alpaca->save(keyHeader, "maxRealPos", MotorFocus.maxRealPos=float(v));
        }
        CFocuser::subSetup(Alpaca, sock, get, data, s);
        // Here I add the setup table in the html...
        s.printf("<h1>Motor</h1>"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "<table align=\"center\">"
            "  <tr><td align=\"right\"><label for=\"FocMax\">max steps:</label></td>"
            "      <td><input type=\"text\" id=\"FocMax\" name=\"FocMax\" value=\"%d\"></td>"
            "  <tr><td align=\"right\"><label for=\"FocMaxSpd\">Speed Steps/s:</label></td>"
            "      <td><input type=\"text\" id=\"FocMaxSpd\" name=\"FocMaxSpd\" value=\"%d\"></td>"
            "  <tr><td align=\"right\"><label for=\"FocAcc\">Time to full speed in ms:</label></td>"
            "      <td><input type=\"text\" id=\"FocAcc\" name=\"FocAcc\" value=\"%d\"></td>"
            "  <tr><td align=\"right\"><label for=\"FocStep\">focuser length in mm:</label></td>"
            "      <td><input type=\"text\" id=\"FocStep\" name=\"FocStep\" value=\"%d\"></td>"
            "</table>"
            "<input type=\"submit\" value=\"Update\">"
            "</form>",
            get_type(), id,
            get_position(), MotorFocus.maxSpeed, MotorFocus.maxAcceleration, int(MotorFocus.maxRealPos));
    }
};


// Now, more complicated is a telescope example...
// First 2 "motors"
CMotor MotorRa(10000, 400, 800, 0.0f, 360.0f);   // a motor with 10k steps, top speed of 400 steps per second, acceleration of 800 steps/s² and a range from 0 to 360deg
CMotor MotorDec(10000, 400, 800, -90.0f, 90.0f);
// and the telescope class...
class CMyTelescope : public CTelescope { public: CMyTelescope(int id): CTelescope(id, "CdB 500 drivers", "1.0", "CdB500", "CdB500") { }
protected:
    bool canpulseguide() override { return true; } // Indicates whether the telescope can be pulse guided
    TAlpacaErr pulseguide(int dir, int length) override  // Moves the scope in the given $Direction for the given $Duration (ms). . 0: north, 1: south, 2: east, 3: west
    { 
        if (dir==0 && guideratedeclination>0) { MotorDec.guide(guideratedeclination, length); return ALPACA_OK; }
        if (dir==1 && guideratedeclination>0) { MotorDec.guide(-guideratedeclination, length); return ALPACA_OK; }
        if (dir==2 && guideraterightascension>0) { MotorRa.guide(guideraterightascension, length); return ALPACA_OK; }
        if (dir==3 && guideraterightascension>0) { MotorRa.guide(-guideraterightascension, length); return ALPACA_OK; }
        return ALPACA_ERR_INVALID_VALUE;
    } 
    bool ispulseguiding() override  { return MotorDec.isGuiding || MotorRa.isGuiding;; }; // Indicates whether the telescope is currently executing a PulseGuide command

    bool get_tracking() override { return MotorRa.tracking!=0; } // Indicates whether the telescope is tracking.
    TAlpacaErr set_tracking(bool v) override { if (v) MotorRa.tracking= trackingrate+1; else MotorRa.tracking= 0; return ALPACA_OK; } // Enables or disables telescope $Tracking.
         // 0: sideral, 1: lunar, 2: solar, 3: king (15.0369 arc"/s)
    TAlpacaErr set_trackingrate(int v) override { trackingrate= v; if (MotorRa.tracking!=0) MotorRa.tracking= v+1; return ALPACA_OK; } // Sets the mount's $TrackingRate.

    bool slewing() override { return MotorDec.isMoving()||MotorRa.isMoving(); }
    TAlpacaErr abortslew() override { MotorDec.stop(); MotorRa.stop(); return ALPACA_OK; }; // Immediatley stops a slew in progress.
    TAlpacaErr slewtocoordinatesasync(float ra, float dec) override { MotorRa.goToReal(ra); MotorDec.goToReal(dec); return ALPACA_OK; } // Asynchronously slew to the given equatorial $RightAscension $Declination coordinates.
    TAlpacaErr synctocoordinates(float ra, float dec) override { /*Do your own!*/; return ALPACA_OK; } // Syncs to the given $RightAscension $Declination coordinates.

    TAlpacaErr axisrates(int axis, char *b) override { strcpy(b, "[{\"Maximum\": 0,\"Minimum\": 4}]"); return ALPACA_OK; } // Returns the rates at which the telescope may be moved about the specified $Axis  returns [{"Maximum": 0,"Minimum": 0}] in b (b will be 30 chr long)
    bool canmoveaxis(int axis) override { return true; } // Indicates whether the telescope can move the requested $Axis.
    TAlpacaErr moveaxis(int axis, float rate) override   // Moves a telescope $Axis at the given $Rate.
    { 
        if (axis==0) {} // DO your own!
        else if (axis==1) {} // DO your own!
        return ALPACA_OK; 
    }

    float declination() override { return MotorDec.posInReal(); }; // Returns the mount's declination.
    float rightascension() override { return MotorRa.posInReal(); }; // Returns the mount's right ascension coordinate.

    // As in the focuser example, loads my config data from storage...
    void subLoad(CAlpaca *alpaca) override
    {
        CTelescope::subLoad(alpaca);
        MotorRa.maxPos= alpaca->load(keyHeader, "RAmaxPos", int32_t(MotorRa.maxPos));
        MotorRa.maxSpeed= alpaca->load(keyHeader, "RAmaxSpeed", int32_t(MotorRa.maxSpeed));
        MotorRa.maxAcceleration= alpaca->load(keyHeader, "RAmaxAcceleration", int32_t(MotorRa.maxAcceleration));
        MotorDec.maxPos= alpaca->load(keyHeader, "DECmaxPos", int32_t(MotorDec.maxPos));
        MotorDec.maxSpeed= alpaca->load(keyHeader, "DECmaxSpeed", int32_t(MotorDec.maxSpeed));
        MotorDec.maxAcceleration= alpaca->load(keyHeader, "DECmaxAcceleration", int32_t(MotorDec.maxAcceleration));
    }
    // As in the focuser example, overrides the default html setup page creation to allow you to add functions to set the configuration!
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override  // This allows you to add stuff in the HTML or handle inputs...
    {
        CTelescope::subSetup(Alpaca, sock, get, data, s);
        if (data!=nullptr)
        {   // used on http post to change parameters...
            int v;
            v= getIntData(data,"RaMax");    if (v!=-1) alpaca->save(keyHeader, "RAmaxPos", MotorRa.maxPos=v);
            v= getIntData(data,"RaMaxSpd"); if (v!=-1) alpaca->save(keyHeader, "RAmaxSpeed", MotorRa.maxSpeed=v);
            v= getIntData(data,"RaAcc");    if (v!=-1) alpaca->save(keyHeader, "RAmaxAcceleration", MotorRa.maxAcceleration=v);
            
            v= getIntData(data,"DecMax");    if (v!=-1) alpaca->save(keyHeader, "DECmaxPos", MotorDec.maxPos=v);
            v= getIntData(data,"DecMaxSpd"); if (v!=-1) alpaca->save(keyHeader, "DECmaxSpeed", MotorDec.maxSpeed=v);
            v= getIntData(data,"DecAcc");    if (v!=-1) alpaca->save(keyHeader, "DECmaxAcceleration", MotorDec.maxAcceleration=v);
        }
        // add the html for the setup to the being created page...
        s.printf("<h1>Motors</h1>"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "<h2>RA Stepper</h2>"
            "<table align=\"center\">"
            "  <tr><td align=\"right\"><label for=\"RaMax\">Steps for 360deg:</label></td>"
            "      <td><input type=\"text\" id=\"RaMax\" name=\"RaMax\" value=\"%d\"></td>"
            "  <tr><td align=\"right\"><label for=\"RaMaxSpd\">Speed Steps/s:</label></td>"
            "      <td><input type=\"text\" id=\"RaMaxSpd\" name=\"RaMaxSpd\" value=\"%d\"></td>"
            "  <tr><td align=\"right\"><label for=\"RaAcc\">Time to full speed in ms:</label></td>"
            "      <td><input type=\"text\" id=\"RaAcc\" name=\"RaAcc\" value=\"%d\"></td>"
            "</table>"
            "<h2>Declinaison Stepper</h2>"
            "<table align=\"center\">"
            "  <tr><td align=\"right\"><label for=\"DecMax\">Steps for 360deg:</label></td>"
            "      <td><input type=\"text\" id=\"DecMax\" name=\"DecMax\" value=\"%d\"></td>"
            "  <tr><td align=\"right\"><label for=\"DecMaxSpd\">Speed Steps/s:</label></td>"
            "      <td><input type=\"text\" id=\"DecMaxSpd\" name=\"DecMaxSpd\" value=\"%d\"></td>"
            "  <tr><td align=\"right\"><label for=\"DecAcc\">Time to full speed in ms:</label></td>"
            "      <td><input type=\"text\" id=\"DecAcc\" name=\"DecAcc\" value=\"%d\"></td>"
            "</table>"
            "<input type=\"submit\" value=\"Update\">"
            "</form>",

            get_type(), id,
            MotorRa.maxPos, MotorRa.maxSpeed, MotorRa.maxAcceleration, 
            MotorDec.maxPos, MotorDec.maxSpeed, MotorDec.maxAcceleration);
        s+= "</p>";
    }

};


// Now, prety much all of this is standard esp32 stuff to start wifi in AP or STA mode...
uint32_t ipaddr;
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) { esp_wifi_connect(); ipaddr = 0; } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) ipaddr = ((ip_event_got_ip_t*) event_data)->ip_info.ip.addr;
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) ipaddr= 0x0104A8C0;
}

static wifi_init_config_t wificfg;
void startWifi(const char *net, const char *pass, const char *hostname, bool accessPoint)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { ESP_ERROR_CHECK(nvs_flash_erase()); ret = nvs_flash_init(); }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (accessPoint) esp_netif_set_hostname(esp_netif_create_default_wifi_ap(), hostname);
    else esp_netif_set_hostname(esp_netif_create_default_wifi_sta(), hostname);

    wificfg = WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&wificfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config; memset(&wifi_config, 0, sizeof(wifi_config));

    if (!accessPoint) 
    {
        wifi_config.sta.threshold.authmode = pass[0]!=0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        strncpy((char *)wifi_config.sta.ssid, net, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    } else {
        strncpy((char*)wifi_config.ap.ssid, net, sizeof(wifi_config.ap.ssid)-1);
        strncat((char*)wifi_config.ap.ssid, "_AP", sizeof(wifi_config.ap.ssid)-1);
        wifi_config.ap.ssid_len= strlen(net)+3;
        wifi_config.ap.channel = 6;
        wifi_config.ap.max_connection = 4,
        wifi_config.ap.authmode = WIFI_AUTH_OPEN, // WIFI_AUTH_WPA2_PSK,
        wifi_config.ap.pmf_cfg.required = true;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
}

extern "C" void app_main()
{
    // Create the alpaca system. This allows access to the wifi setup....
    CAlpaca *alpaca= new CAlpaca("CdBTelescopeServer", "CdB", "Alpaca CdB eq telescope", "Ardeche");
    startWifi(alpaca->wifi, alpaca->wifip, "eqControl", alpaca->load("APMode", int32_t(1))!=0);
    // Create and add 2 devices to alpaca (with id 0 for both as the id is on a perdevice type basis)
    alpaca->addDevice(new CMyTelescope(0));
    alpaca->addDevice(new CMyFocuser(0));
    // and start the http and udp servers!
    alpaca->start(80);
}
