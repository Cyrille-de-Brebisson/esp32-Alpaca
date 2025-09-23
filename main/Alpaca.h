/***************************************************************
* Cyrille.de.Brebisson@gmail.com implementation of Ascom Alpaca device side
* This code assumes very little and uses no external libraries
* It does rely on sockets existing as well as basic C stuff (sprintf) but that is about it
* The original design was for esp32, as a result it does use the esp32 Non Volatile Storage (nvs) library
* but you can also compile it for windows (if _WIN32 is defined). At that point storage will be done in a crapy "Alpaca" file format...
*
* To use, I would advise you to look at an example, but here is a quick break down:
* First, make sure you have a network connect
* Second, define objects that derive from the device classes that you are interested in and implement the appropriate functions
* Here is a focusser example:
* class CMyFocuser : public CFocuser
* { public:
*     CMyFocuser(int id): CFocuser(id, "CdB Focuser Driver", "1", "CdB Alpaca Focuser", "Focuser for eqMount") { }
*     bool get_absolute() override { return true; }
*     bool get_ismoving() override { return MotorFocus.isMoving(); }
*     int32_t get_maxincrement() override { return MotorFocus.maxPos; }
*     int32_t get_maxstep() override { return MotorFocus.maxPos; }
*     int32_t get_position() override { return MotorFocus.pos; }
*     int32_t get_stepsize() override { return FocusStepum; }
*     TAlpacaErr put_halt() override { MotorFocus.stop(); return ALPACA_OK; };
*     TAlpacaErr put_move(int32_t position) override { MotorFocus.goToSteps(position); return ALPACA_OK; };
* };
* Then, in your main, do this:
*  CAlpaca *alpaca= new CAlpaca("CdB", "1", "FocusServer", "Mars");  // Create the Alpaca system
*  alpaca->addDevice(new CMyFocuser(0)); // Add up to 10 devices (this can be changed if needed). But the device ID (one counter per type), has to be correct!
*  alpaca->start();  // starts the system
* 
* At this point in time, I have done the Focusser, FilterWheel and Telescope
* Dispatch and subSetup will need to be created for any other device type... Probably a 20 mn job per device...
* Have fun
* 
* Note: NO HTTP INPUT IS DEFENSIVELY VALIDATED!!!! NONE of the string output from device is verified either (but this is more your fault)
* One could argue that this is "bad" and that I am a shitty programmer that does not understand security...
* I would argue back that Alpaca allows ANYONE with net access to manipulate HW which can be worth thousand
* and that this is WAY more risky than a "bad actor" crashing the system! If I was a bad actor, I would 
* use the system to open domes when it rains or some other crap.... which is way worse than a FW bug...
* Anyway, if you plan to use this code in a "serious" setting. it won't be too hard to fix (probably a day's work).
* 
* For testing, you can of course compile and run this under windows!
* You will see some #ifndef _WIN32 throughout these files which are designed exactly for that...
****************************************************************/

#define _CRT_SECURE_NO_WARNINGS // allows windows compilation
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#ifndef _WIN32
#include "nvs_flash.h" // esp32 data storage...
#include "nvs.h"
#else
class CPreference { public: // A bad preference class for use in windows usesd to permaently save setting, limited to 200 keys...
    struct { char k[32]; int32_t v; } ints[100]; int usedInts= 0;
    struct { char k[32], v[32]; } texts[100]; int usedText= 0;
    void load(char const* fn); void save(char const* fn);
};
typedef int nvs_handle_t;
#endif

class CAlpacaDevice; // Generic class for all Alpaca devices..

//////////////////////////////////////////////////////
// The MAIN class!!!!
// change maxDevices if you need
// in order, create, call addDevice n times, and then start
class CAlpaca { public:
    static int const maxDevices=10;
    // Note that DefaultServerName and DefaultLocation can be overriden by user changing these in the setup. They will then be saved and
    // reloaded by the system...
    CAlpaca(char const *Manufacturer, char const *ManufacturerVersion, char const *DefaultServerName, char const *DefaultLocation);
    void addDevice(CAlpacaDevice *a); // add a device to the system.
    void start(int httpPort=80);      // starts the system!

    // When the user http get/put /setup, this gets executed
    virtual bool setup(int sock, bool get, char *data);

    // this uses the non volatile storage API helpers to load/save data...
    // You can just call load or save, but if you have a number of load/save to do in a row
    // you can call saveLoadBegin first, then all your operations, then saveLoadEnd to commit. Will be faster...
    // all load operations have a "default" value should you need it...
    // This is there because it is used by the default setup stuff...
    void saveLoadBegin();
    void saveLoadEnd();
    void save(char const *key, char const *v);
    void save(char const *key, int32_t v);
    void save(char const *key, int v) { save(key, int32_t(v)); }
    void save(char const *key, float v);
    void save(char const *key, uint8_t const *v, int size);
    // These are the same, but they handle a key spread on 2 strings. For example a "header" and "key"
    template <typename T> void save(char const *key1, char const *key2, T v) { char t[100]; strcpy(t, key1); strcat(t, key2); save(t, v); }
    void save(char const *key1, char const *key2, uint8_t const *v, int size)  { char t[100]; strcpy(t, key1); strcat(t, key2); save(t, v, size); }
    // all the loads have a default value returned if error...
    char *load(char const *key, char const *def, char *buf, size_t buflen); // put data in buf. return it also for convinience... buf MUST be large enough for def
    bool load(char const *key, uint8_t *buf, size_t buflen); // load data in buf. return true if successful
    int32_t load(char const *key, int32_t def);
    int load(char const *key, int def) { return load(key, int32_t(def)); }
    float load(char const *key, float def);
    // These are the same, but they handle a key spread on 2 strings. For example a "header" and "key"
    template <typename T> T load(char const *key1, char const *key2, T v) { char t[100]; strcpy(t, key1); strcat(t, key2); return load(t, v); }
    bool load(char const *key1, char const *key2, uint8_t *buf, int buflen) { char t[100]; strcpy(t, key1); strcat(t, key2); return load(t, buf, buflen); }
    char *load(char const *key1, char const *key2, char const *def, char *buf, size_t buflen) { char t[100]; strcpy(t, key1); strcat(t, key2); return load(t, def, buf, buflen); }


    // These should be private with a friend declaration... but I am too lazy!
    int volatile newClientSocket; // will be set to the next client socket to talk to...
    int httpport;
    // This one is called by the http client to execute requests...
    // return true if no errors and connection can stay alive
    bool execRequest(int sock, bool put, char *url, char *data);

    // User settings...
    char ServerName[32], Manufacturer[32], ManufacturerVersion[32], Location[32], wifi[32], wifip[32];
private:
    CAlpacaDevice *devices[maxDevices]; int nbDevices= 0; // The device list
    // devile lookup from url. Assumes url points to Focusser/0/ for example, return device and url after the id and the last '/'
    // else return nullprt. url will not have moved...
    CAlpacaDevice *deviceFromURL(char *&url); 
    char uniqueid[13]; // designed for mac address...
    void osinit(); // os specific intialisation

    // for data saving...
    nvs_handle_t saveLoadHandle= 0xffffffff; bool saveLoadHandleDirty= false; int saveLoadCount= 0;
};


enum TAlpacaErr { // Alpaca error code for returns if you need them...
    ALPACA_OK                                 = 0x000,
    ALPACA_ERR_NOT_IMPLEMENTED                = 0x400,
    ALPACA_ERR_INVALID_VALUE                  = 0x401,
    ALPACA_ERR_VALUE_NOT_SET                  = 0x402,
    ALPACA_ERR_NOT_CONNECTED                  = 0x407,
    ALPACA_ERR_INVALID_WHILE_PARKED           = 0x408,
    ALPACA_ERR_INVALID_WHILE_SLAVED           = 0x409,
    ALPACA_ERR_INVALID_OPERATION              = 0x40B,
    ALPACA_ERR_ACTION_NOT_IMPLEMENTED         = 0x40C};

// Used by dispatch. This is a string class that grows by packs of 1K... allows to not have any dependencies...
class CMyStr { public:
    char *c= nullptr; size_t csize= 0, w= 0;
    CMyStr() { c= (char*)malloc(csize= 1024); }
    ~CMyStr() { free(c); }
    void grow() { c= (char*)realloc(c, csize= csize+1024); }
    void append(char const *s, size_t l=-1)
    {
        if (l==-1) l=strlen(s);
        if (w+l>csize-1) grow();
        memcpy(c+w, s, l); w+= l;
    }
    CMyStr &operator +=(char const *s) { append(s); return *this; }
    void printf(const char* format, ...)
    {
        va_list args; va_start(args, format);
        size_t l= vsnprintf(nullptr, 0, format, args)+1;
        c= (char*)realloc(c, csize= csize+l+1024);
        vsprintf(c+w, format, args); va_end(args);
        w+= strlen(c+w);
    }
};

// copy s into d (size dlen) and make sure that there is a terminal 0 and no overwrite
static void strncpy2(char *d, char const *s, int dlen) { d[dlen-1]= 0; strncpy(d, s, dlen-1); }

// This is the generic Alpaca device class. All device types inherits from it...
class CAlpacaDevice { public:
    CAlpacaDevice(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription):id(id), driverInfo(driverInfo), driverVersion(driverVersion)
    { 
        strncpy2(Name, defaultName, sizeof(Name));
        strncpy2(Description, defaultDescription, sizeof(Description));
        _jsonDesc[0]= 0; // Can not generate here as C/C++ does not allow for virtual function calls in constructor (bad design! Pascal can!)
    }
    CAlpaca *alpaca= nullptr; // back pointer to owner...

    ///////////////////////////////////
    // Here you have a bunch of virtual functions that you COULD override if you wanted to... But in 99% of cases the default impementation is good enough...
        bool connected= false;
    virtual bool get_connected() { return connected; return ALPACA_OK; }
    virtual TAlpacaErr set_connected(bool connected) { this->connected= connected; return ALPACA_OK; }
    virtual bool get_connecting() { return false; return ALPACA_OK; }

    // If you want to use these, GO SEE THE DISPATCH and now action/command/parameters is handled because I am 100% certain that it will not work (as I have no use for it, I never checked)...
    // Also, all char data here is RAW data from the http request.
    // It is a pointer on the start of the text, and can continue past the real end...
    // Basically, this is what I get as input: Action=sddf%25%25&Parameters=sdfssdfsdf 
    // And action will be a pointer after Action= while parameters will be a pointer after Parameters=. Parse and sanitize as needed!
    // Look at the getHtmlString as it is mostly what you need/want...
    virtual TAlpacaErr action(const char *action, const char *parameters) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual TAlpacaErr commandblind(const char *command) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual TAlpacaErr commandbool(const char *command, bool *resp) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual TAlpacaErr commandstring(const char *action, char *buf, size_t len) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }

    virtual char const *get_description() { return Description; }
    virtual char const *get_driverinfo() { return driverInfo; }
    virtual char const *get_driverversion() { return driverVersion; }
    virtual char const *get_name() { return Name; }
    virtual uint32_t get_interfaceversion() = 0;
    virtual char const *get_supportedactions() { return "[]"; } // return json OK array of string....
    ///////////////////////////////////


    // All classes will have a dispatch which will transform http requests into function calls...
    // As a general rule, inheriting classes will first test their known API and then default to the upperclass one...
    // get is true for http GET an false for http PUT
    // url is the url. Usually points on the command, or at least after the api/v1/type/id/ stuff
    // data would be any parameters raw from http
    // s is the json file that will be returned
    // return false to have the system return an error 400...
    virtual bool dispatch(bool get, char const *url, char *data, CMyStr *s);

    // call to the setup api. return false to have the system return err404. else send what you want through sock and return true to continue the connection...
    // get is true if get, false if put. data is the data passed to the server
    virtual bool setup(CAlpaca *Alpaca, int sock, bool get, char *data); 
    // subSetup  allows you to add stuff in the setup HTML or handle inputs... This is usually what you will want to change if you want to add to the default setup page
    virtual void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) { }

    // Allows you to load stuff from persistance if you want...
    // see example in FilterWheel. Do not forget to use keyHeader as you might have multiple devices of the same type in your system!
    virtual void subLoad(CAlpaca *alpaca) { }


    // You should NOT have to touch any of this as this is internal stuff
    // most of stuff here should be private, but it's only used by Alpaca in one spot... and I can't be bother to friend it...
    int id;
    char const *driverInfo, *driverVersion;
    char Name[32], Description[32];
    virtual char const *get_type()= 0; 
    char _jsonDesc[512];          // Will hold afer the addition of the device to alpaca the device description for telling clients about who you are... initialized once...
    char keyHeader[10];           // Will contain some text used to prefix any key for load/save... see load/save in addDevice and setup...
};

//////////////////////////////////
// now, you have all the supported device types!
// see alpaca documention to know what every function does...

// This one works...
class CFilterWheel : public CAlpacaDevice { public: 
    CFilterWheel(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) 
    { strcpy(focusoffsets, "[]"); strcpy(names, "[]"); }
    uint32_t get_interfaceversion() override { return 3; }
    virtual char const *get_focusoffsets() { return focusoffsets; }; // returns a json formated array of integer values... "[1, 2, 3]" for example
    virtual char const *get_names() {return names; };  // returns a json formated array of string values... "["S", "H", "O"]" for example
    virtual int get_position() = 0;
    virtual TAlpacaErr put_position(int32_t position) = 0;
    void subLoad(CAlpaca *alpaca) override;

    bool dispatch(bool get, char const *url, char *data, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override;  // This allows you to add stuff in the HTML or handle inputs...
protected:
    char const *get_type() override { return "FilterWheel"; }
    char focusoffsets[128], names[128];
};

// This one works...
class CFocuser : public CAlpacaDevice { public: 
    CFocuser(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 4; }
    virtual bool get_absolute() = 0;
    virtual bool get_ismoving() = 0;
    virtual int32_t get_maxincrement() = 0;
    virtual int32_t get_maxstep() = 0;
    virtual int32_t get_position() = 0;
    virtual int32_t get_stepsize() { return alpaca->load(keyHeader, "FocStepSize", int32_t(6)); }
    virtual TAlpacaErr put_halt() = 0;
    virtual TAlpacaErr put_move(int32_t position) = 0;
    virtual TAlpacaErr get_tempcomp(bool *tempcomp) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual TAlpacaErr put_tempcomp(bool tempcomp) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual bool get_tempcompavailable() { return false; }
    virtual TAlpacaErr get_temperature(double *temperature) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }

    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
protected:
    char const *get_type() override { return "Focuser"; }
};

// This has never been tested and will not work as I have not written the dispatch!
class CCamera : public CAlpacaDevice { public: CCamera(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
protected:
    uint32_t get_interfaceversion() override { return 4; }
    char const *get_type() override { return "Camera"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested and will not work as I have not written the dispatch!
class CCoverCalibrator : public CAlpacaDevice { public: CCoverCalibrator(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 2; }
    uint32_t brightness= 0;
    virtual TAlpacaErr get_brightness(uint32_t *brightness) { *brightness= this->brightness; return ALPACA_OK; } // ok in most cases...
    virtual TAlpacaErr get_calibratorchanging(bool *changing) { *changing= false; return ALPACA_OK; } // brightness is changing... In most cases it is instantaneous...
      enum class CalibratorState { NotPresent, Off, NotReady, Ready, Unknown, Error };
    virtual TAlpacaErr get_calibratorstate(CalibratorState *state) { *state= CalibratorState::Ready; return ALPACA_OK; } // valid in most casers...
      enum CoverState { NotPresent, Closed, Moving, Open, Unknown, Error };
    virtual TAlpacaErr get_coverstate(CoverState *state) = 0;
    virtual TAlpacaErr get_covermoving(bool *moving) { CoverState s; TAlpacaErr er= get_coverstate(&s); if (er!=ALPACA_OK) return er; *moving= s==Moving; return ALPACA_OK; } // ok in most cases...
    virtual TAlpacaErr get_maxbrightness(uint32_t *max) = 0;
    virtual TAlpacaErr calibratoroff() { return calibratoron(0); } // ok for most cases...
    virtual TAlpacaErr calibratoron(int32_t brightness) { this->brightness= brightness; return ALPACA_OK; } // overide as needed, but do not forget to set brightness!!!
    virtual TAlpacaErr closecover() = 0;
    virtual TAlpacaErr opencover() = 0;
    virtual TAlpacaErr haltcover() = 0;
protected:
    char const *get_type() override { return "CoverCalibrator"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested and will not work as I have not written the dispatch!
class CDome : public CAlpacaDevice { public: CDome(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 3; }
    virtual TAlpacaErr get_altitude(float *altitude) = 0;
    virtual TAlpacaErr get_athome(bool *athome) = 0;
    virtual TAlpacaErr get_atpark(bool *atpark) = 0;
    virtual TAlpacaErr get_azimuth(float *azimuth) = 0;
    virtual TAlpacaErr get_canfindhome(bool *canfindhome) = 0;
    virtual TAlpacaErr get_canpark(bool *canpark) = 0;
    virtual TAlpacaErr get_cansetaltitude(bool *cansetaltitude) = 0;
    virtual TAlpacaErr get_cansetazimuth(bool *cansetazimuth) = 0;
    virtual TAlpacaErr get_cansetpark(bool *cansetpark) = 0;
    virtual TAlpacaErr get_cansetshutter(bool *cansetshutter) = 0;
    virtual TAlpacaErr get_canslave(bool *canslave) = 0;
    virtual TAlpacaErr get_cansyncazimuth(bool *cansyncazimuth) = 0;
      enum ShutterState { Open, Closed, Opening, Closing, Error };
    virtual TAlpacaErr get_shutterstatus(ShutterState *shutterstatus) = 0;
    virtual TAlpacaErr get_slaved(bool *slaved) = 0;
    virtual TAlpacaErr put_slaved(bool slaved) = 0;
    virtual TAlpacaErr get_slewing(bool *slewing) = 0;
    virtual TAlpacaErr put_abortslew() = 0;
    virtual TAlpacaErr put_closeshutter() = 0;
    virtual TAlpacaErr put_findhome() = 0;
    virtual TAlpacaErr put_openshutter() = 0;
    virtual TAlpacaErr put_park() = 0;
    virtual TAlpacaErr put_setpark() = 0;
    virtual TAlpacaErr put_slewtoaltitude(float altitude) = 0;
    virtual TAlpacaErr put_slewtoazimuth(float azimuth) = 0;
    virtual TAlpacaErr put_synctoazimuth(float azimuth) = 0;
protected:
    char const *get_type() override { return "Dome"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested and will not work as I have not written the dispatch!
class CObservingConditions : public CAlpacaDevice { public: CObservingConditions(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 2; }
    virtual TAlpacaErr get_averageperiod(double *averageperiod) { *averageperiod= this->averageperiod; return ALPACA_OK; }
    virtual TAlpacaErr put_averageperiod(double averageperiod) { this->averageperiod= averageperiod;  return ALPACA_OK; }
    virtual TAlpacaErr get_cloudcover(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_dewpoint(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_humidity(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_pressure(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_rainrate(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_skybrightness(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_skyquality(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_skytemperature(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_starfwhm(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_temperature(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_winddirection(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_windgust(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_windspeed(double *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr put_refresh() = 0;
    virtual TAlpacaErr get_sensordescription(char const *sensorname, char const *&buf) = 0; // return a pointer to the description for the requested sensor... Typically the specs...
    virtual TAlpacaErr get_timesincelastupdate(double *timesincelastupdate) = 0;
    protected:
    double averageperiod= 1.0/24.0f/60.0f; // default is 1 minute..
    char const *get_type() override { return "ObservingConditions"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested but is implemented
class CRotator : public CAlpacaDevice { public: CRotator(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 4; }
    // Assumes that there is a mechanical position and will handle relative position as an offset to it for you...
    // So you only need to implement these 5 functions...
    virtual TAlpacaErr get_mechanicalposition(double *mechanicalposition) = 0;
    virtual TAlpacaErr put_movemechanical(double position) = 0;
    virtual TAlpacaErr get_stepsize(double *stepsize) = 0;
    virtual TAlpacaErr get_ismoving(bool *ismoving) = 0;
    virtual TAlpacaErr put_halt() = 0;
    // handle relative to absolue for you!
    virtual TAlpacaErr get_position(double *position) { TAlpacaErr er= get_mechanicalposition(position); if (er==ALPACA_OK) *position+= offset; return er; }
    virtual TAlpacaErr put_sync(double position) { double pos; get_mechanicalposition(&pos); offset= position-pos; return ALPACA_OK; }
    virtual TAlpacaErr get_targetposition(double *targetposition) { *targetposition= this->targetposition; return ALPACA_OK;}
    virtual TAlpacaErr put_move(double position) { double pos; get_position(&pos); return put_moveabsolute(pos+position); }
    virtual TAlpacaErr put_moveabsolute(double position) { return put_movemechanical(targetposition=position-offset); }
    // Assumes that you can reverse and will handle saving this for you
    virtual TAlpacaErr get_canreverse(bool *canreverse) { *canreverse= true; return ALPACA_OK; }
    virtual TAlpacaErr get_reverse(bool *reverse) { bool can; get_canreverse(&can); if (!can) return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; *reverse= this->reverse; return ALPACA_OK; }
    virtual TAlpacaErr put_reverse(bool reverse)  { bool can; get_canreverse(&can); if (!can) return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; this->reverse= reverse; return ALPACA_OK; }
protected:
    bool reverse= false;
    double offset= 0.0f, targetposition= 0.0f;
    char const *get_type() override { return "Rotator"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested but is implemented
class CSafetyMonitor : public CAlpacaDevice { public: CSafetyMonitor(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 3; }
    virtual TAlpacaErr get_issafe(bool *issafe) = 0;
protected:
    char const *get_type() override { return "SafetyMonitor"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested but is implemented
class CSwitch : public CAlpacaDevice { public: CSwitch(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 3; }
    // Let us assume that you want to use default helpers...
    // override the defaultSwitchDefs function to return the default switches description.
    // then override the put_setswitch and put_setswitchvalue to act on the changes...
    // you might also want to work on the subInit to change the initialization value if not equal to the minswitchvalues...
      struct TSwitchDef { bool canwrite, canasync; char const*switchname, *switchdescription; float switchstep=1.0f, minswitchvalue=0.0f, maxswitchvalue=1.0f; };
    virtual TSwitchDef const *defaultSwitchDefs(int32_t &nb) { nb= 0; return nullptr; }

    virtual TAlpacaErr get_maxswitch(int32_t *maxswitch) { init(0); *maxswitch= nbswitches; return ALPACA_OK; }
    // get info on the various switches... Most is goten from the struct returned by the defaultSwitchDefs function. But names and description can be overriden by the user...
    virtual TAlpacaErr get_canwrite(int32_t id, bool *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].canwrite; return ALPACA_OK; }
    virtual TAlpacaErr get_getswitchdescription(int32_t id, char *buf, size_t len) 
    { 
        if (!init(id)) return ALPACA_ERR_INVALID_VALUE; 
        strncpy2(buf, switches[id].switchdescription, len); 
        char t[20]; sprintf(t, "%ldName", id); alpaca->load(keyHeader, t, buf, buf, len);
        return ALPACA_OK; 
    }
    virtual TAlpacaErr get_getswitchname(int32_t id, char *buf, size_t len) 
    { 
        if (!init(id)) return ALPACA_ERR_INVALID_VALUE; 
        strncpy2(buf, switches[id].switchdescription, len); 
        char t[20]; sprintf(t, "%ldDesc", id); alpaca->load(keyHeader, t, buf, buf, len);
        return ALPACA_OK; 
    }
    virtual TAlpacaErr put_setswitchname(int32_t id, const char *name) { char t[20]; sprintf(t, "%ldName", id); alpaca->save(keyHeader, t, name); return ALPACA_OK; }
    virtual TAlpacaErr put_setswitchdescription(int32_t id, const char *name) { char t[20]; sprintf(t, "%ldDesc", id); alpaca->save(keyHeader, t, name); return ALPACA_OK; }
    // get/set bool value
    virtual TAlpacaErr get_getswitch(int32_t id, bool *value)  { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switchesValues[id]!=0; return ALPACA_OK; }
    virtual TAlpacaErr put_setswitch(int32_t id, bool value) 
    { 
        double m, M; if (get_minswitchvalue(id, &m)!=ALPACA_OK) return ALPACA_ERR_INVALID_VALUE; get_maxswitchvalue(id, &M);
        switchChanged(id, switchesValues[id]= value?M:m);
        return ALPACA_OK; 
    }
    // handle variable switches
    virtual TAlpacaErr get_switchstep(int32_t id, double *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].switchstep; return ALPACA_OK; }
    virtual TAlpacaErr get_minswitchvalue(int32_t id, double *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].minswitchvalue; return ALPACA_OK; }
    virtual TAlpacaErr get_maxswitchvalue(int32_t id, double *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].maxswitchvalue; return ALPACA_OK; }
    virtual TAlpacaErr get_getswitchvalue(int32_t id, double *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switchesValues[id]; return ALPACA_OK; }
    virtual TAlpacaErr put_setswitchvalue(int32_t id, double value) 
    { 
        if (!init(id)) return ALPACA_ERR_INVALID_VALUE; 
        double m, M; get_minswitchvalue(id, &m); get_maxswitchvalue(id, &M);
        if (value<m || value>M) return ALPACA_ERR_INVALID_VALUE;
        switchesValues[id]= value;
        switchChanged(id, value);
        return ALPACA_OK; 
    }
    // async operations... These are not implemented at all at this point. If you need them... contact me :-)
    virtual TAlpacaErr get_canasync(int32_t id, bool *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].canasync; return ALPACA_OK; }
    virtual TAlpacaErr put_setasync(int32_t id, bool state) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual TAlpacaErr put_setasyncvalue(int32_t id, double value) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_statechangecomplete(int32_t id, bool *completed) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }

protected:
    int32_t nbswitches=0; TSwitchDef const *switches= nullptr;
    double *switchesValues= nullptr;
    virtual void switchChanged(int32_t id, double value) = 0; // override this to act on switch value changed. value is min/max switch values if the setswitch(bool) function is used.
    virtual void subInit() {} // override to do something when the default values are read. For example you can use that to change the current switches values at startup...
    bool init(int id) // makes sure that everything is initialized properly and return true if id is a valid switch id!
    {
        if (nbswitches==0)
        {
            switches= defaultSwitchDefs(nbswitches);
            switchesValues= (double*)malloc(nbswitches*sizeof(double));
            for (int i=0; i<nbswitches; i++) switchesValues[i]= switches[i].minswitchvalue;
            subInit(); // allows configuraiton...
        }
        return id>=0 && id<nbswitches;
    }
    char const *get_type() override { return "Switch"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This been tested and works :-) for once!
class CTelescope : public CAlpacaDevice { public: CTelescope(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
protected:
    uint32_t get_interfaceversion() override { return 4; }
    char const *get_type() override { return "Telescope"; }

    virtual int alignmentmode() { return 2; } // Returns the current mount alignment mode  //0: altAz, 1: polar, 2: germanPolar(need meridian flip)
    virtual int equatorialsystem() { return 2; } // Returns the current equatorial coordinate system used by this telescope. (0: Custom or unknown equinox and/or reference frame. 1; Topocentric coordinates. 2: J2000 equator/equinox. 3: J2050 equator/equinox. 4: B1950 equinox, FK4 reference frame.)

    virtual float get_aperturearea() { return alpaca->load(keyHeader, "aperturearea", 0.062f*0.062f/4.0f*3.14f); } // Returns the telescope's aperture.
    virtual TAlpacaErr set_aperturearea(float v) { alpaca->save(keyHeader, "aperturearea", v); return ALPACA_OK; } // Returns the telescope's aperture.
    virtual float get_aperturediameter() { return alpaca->load(keyHeader, "aperturediameter", 0.062f); } // Returns the telescope's effective aperture.
    virtual TAlpacaErr set_aperturediameter(float v) { alpaca->save(keyHeader, "aperturediameter", v); return ALPACA_OK; } // Returns the telescope's effective aperture.
    virtual float get_focallength() { return alpaca->load(keyHeader, "focallength", 400.0f); } // Returns the telescope's focal length in meters.
    virtual TAlpacaErr set_focallength(float v) { alpaca->save(keyHeader, "focallength", v); return ALPACA_OK;  } // Returns the telescope's focal length in meters.
    virtual float get_siteelevation() { return alpaca->load(keyHeader, "siteelevation", 1060.0f); } // Returns the observing $SiteElevation above mean sea level.
    virtual TAlpacaErr set_siteelevation(float v) { alpaca->save(keyHeader, "set_siteelevation", v); return ALPACA_OK; } // Sets the observing site's elevation above mean sea level.
    virtual float get_sitelatitude() { return alpaca->load(keyHeader, "sitelatitude", 45.007109f); } // Returns the observing $SiteLatitude .
    virtual TAlpacaErr set_sitelatitude(float v) { alpaca->save(keyHeader, "sitelatitude", v); return ALPACA_OK; } // Sets the observing site's latitude.
    virtual float get_sitelongitude() { return alpaca->load(keyHeader, "sitelongitude", 4.335247f); } // Returns the observing site's longitude.
    virtual TAlpacaErr set_sitelongitude(float v) { alpaca->save(keyHeader, "sitelongitude", v); return ALPACA_OK; } // Sets the observing $SiteLongitude .

    virtual bool canfindhome() { return false; } // Indicates whether the mount can find the home position.
    virtual bool athome() { return false; } // Indicates whether the mount is at the home position.
    virtual TAlpacaErr findhome()  { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Moves the mount to the "home" position.

    virtual bool canpark() { return false; } // Indicates whether the telescope can be parked.
    virtual bool atpark() { return false; } // Indicates whether the telescope is at the park position.
    virtual bool cansetpark() { return false; } // Indicates whether the telescope park position can be set.
    virtual bool canunpark() { return false; } // Indicates whether the telescope can be unparked.
    virtual TAlpacaErr park() { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }// Park the mount
    virtual TAlpacaErr setpark() { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }// Sets the telescope's park position as current's position
    virtual TAlpacaErr unpark() { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }// Unparks the mount.

    virtual bool canpulseguide() { return true; } // Indicates whether the telescope can be pulse guided
    virtual TAlpacaErr pulseguide(int dir, int length) = 0; // Moves the scope in the given $Direction for the given $Duration (ms). . 0: north, 1: south, 2: east, 3: west
    virtual bool ispulseguiding() = 0; // Indicates whether the telescope is currently executing a PulseGuide command
    virtual bool cansetguiderates() { return true; } // Indicates whether the DeclinationRate property can be changed.
        float guideratedeclination= -1, guideraterightascension= -1;
    virtual float get_guideratedeclination() { if (guideratedeclination<0) guideratedeclination= alpaca->load(keyHeader, "guideratedeclination", 7.5f/3600.0f); return guideratedeclination; }  // Returns the current Declination rate offset for telescope guiding
    virtual TAlpacaErr set_guideratedeclination(float v)  { guideratedeclination= v; alpaca->save(keyHeader, "guideratedeclination", guideratedeclination); return ALPACA_OK; } // Sets the current $GuideRateDeclination rate offset for telescope guiding.
    virtual float get_guideraterightascension()  { if (guideraterightascension<0) guideraterightascension= alpaca->load(keyHeader, "guideraterightascension", 7.5f/3600.0f); return guideraterightascension; } // Returns the current RightAscension rate offset for telescope guiding
    virtual TAlpacaErr set_guideraterightascension(float v) { guideraterightascension= v; alpaca->save(keyHeader, "guideraterightascension", guideraterightascension); return ALPACA_OK; } // Sets the current $GuideRateRightAscension  rate offset for telescope guiding.

    virtual bool cansettracking() { return true; } // Indicates whether the Tracking property can be changed.
        bool tracking= true;
    virtual bool get_tracking() { return tracking; } // Indicates whether the telescope is tracking.
    virtual TAlpacaErr set_tracking(bool v) { tracking= v; return ALPACA_OK; } // Enables or disables telescope $Tracking.
        int trackingrate= 0; // 0: sideral, 1: lunar, 2: solar, 3: king (15.0369 arc"/s)
    virtual int get_trackingrate() { return trackingrate; } // Returns the current tracking rate. 0: sideral, 1: lunar, 2: solar, 3: king (15.0369 arc"/s)
    virtual TAlpacaErr set_trackingrate(int v) { trackingrate= v; return ALPACA_OK; } // Sets the mount's $TrackingRate.
    virtual char const *trackingrates() { return "[0,1,2,3]"; } // Returns a collection of supported DriveRates values. i.e: a json object that has 0 to 4 integers in it...

    virtual bool cansetrightascensionrate() { return false; } // Indicates whether the RightAscensionRate property can be changed.
    virtual float get_rightascensionrate() { return 0.0f; } // Returns the telescope's right ascension tracking rate.
    virtual TAlpacaErr set_rightascensionrate(float v) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Sets the telescope's $RightAscensionRate tracking rate.
    virtual bool cansetdeclinationrate() { return false; } // Indicates whether the DeclinationRate property can be changed.
    virtual float get_declinationrate() { return 0.0f; } // Returns the telescope's declination tracking rate.
    virtual TAlpacaErr set_declinationrate(float v) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Sets the telescope's $DeclinationRate tracking rate.
         
    virtual bool slewing() = 0; // Indicates whether the telescope is currently slewing.
    virtual TAlpacaErr set_slewsettletime(int32_t v) { alpaca->save(keyHeader, "slewsettletime", v); return ALPACA_OK; }// Sets the post-slew $SlewSettleTime time.
    virtual int get_slewsettletime() { return alpaca->load(keyHeader, "slewsettletime", int32_t(1)); } // Returns the post-slew settling time (sec)
    virtual TAlpacaErr abortslew() = 0; // Immediatley stops a slew in progress.
    virtual bool canslew() { return false; } // Indicates whether the telescope can slew synchronously.
    virtual bool canslewasync() { return true; } // Indicates whether the telescope can slew asynchronously.
    virtual TAlpacaErr slewtocoordinates(float ra, float dec)  { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Synchronously slew to the given equatorial coordinates.
    virtual TAlpacaErr slewtocoordinatesasync(float ra, float dec)= 0; // Asynchronously slew to the given equatorial $RightAscension $Declination coordinates.
    virtual bool cansync() { return true; } // Indicates whether the telescope can sync to equatorial coordinates.
    virtual TAlpacaErr synctocoordinates(float ra, float dec)= 0; // Syncs to the given $RightAscension $Declination coordinates.

        float targetdeclination= 0.0f, targetrightascension= 0.0f;
    virtual float get_targetdeclination() { return targetdeclination; } // Returns the current target declination.
    virtual TAlpacaErr set_targetdeclination(float v) { targetdeclination= v; return ALPACA_OK; } // Sets the $TargetDeclination of a slew or sync.
    virtual float get_targetrightascension() { return targetrightascension; } // Returns the current target right ascension.
    virtual TAlpacaErr set_targetrightascension(float v) { targetrightascension= v; return ALPACA_OK; } // Sets the target $TargetRightAscension of a slew or sync.
    virtual TAlpacaErr slewtotarget() { return slewtocoordinates(targetdeclination, targetrightascension); } // Synchronously slew to the TargetRightAscension and TargetDeclination coordinates.
    virtual TAlpacaErr slewtotargetasync() { return slewtocoordinatesasync(targetdeclination, targetrightascension); } // Asynchronously slew to the TargetRightAscension and TargetDeclination coordinates.
    virtual TAlpacaErr synctotarget() { return synctocoordinates(targetdeclination, targetrightascension); } // Syncs to the TargetRightAscension and TargetDeclination coordinates.

    virtual float declination()= 0; // Returns the mount's declination.
    virtual float rightascension()= 0; // Returns the mount's right ascension coordinate.

    virtual bool canslewaltaz() { return false; } // Indicates whether the telescope can slew synchronously to AltAz coordinates.
    virtual bool canslewaltazasync() { return false; } // Indicates whether the telescope can slew asynchronously to AltAz coordinates.
    virtual bool cansyncaltaz() { return false; } // Indicates whether the telescope can sync to local horizontal coordinates.
    virtual TAlpacaErr slewtoaltaz(float az, float al) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Synchronously slew to the given local horizontal $Azimuth $Altitude coordinates.
    virtual TAlpacaErr slewtoaltazasync(float az, float al) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Asynchronously slew to the given local horizontal $Azimuth $Altitude coordinates.
    virtual TAlpacaErr synctoaltaz(float az, float al) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Syncs to the given local horizontal $Azimuth $Altitude coordinates.

    virtual TAlpacaErr altitude(float &v) { v=0.0f; return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }  // Returns the mount's altitude above the horizon.
    virtual TAlpacaErr azimuth(float &v) { v=0.0f; return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Returns the mount's azimuth.

    virtual bool get_doesrefraction() { return false; } // Indicates whether atmospheric refraction is applied to coordinates.
    virtual TAlpacaErr set_doesrefraction(bool v) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Determines whether atmospheric $DoesRefraction is applied to coordinates.

    virtual bool cansetpierside() { return false; } // Indicates whether the telescope SideOfPier can be set.
    virtual int get_sideofpier() { return -1; } // Returns the mount's pointing state. 0:east, 1: west, -1: unknown
    virtual TAlpacaErr set_sideofpier(int v) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }// Sets the mount's $SideOfPier pointing state.
    virtual int destinationsideofpier(float ra, float dec) { return -1; } // Predicts the pointing state after a German equatorial mount slews to given $RightAscension $Declination coordinates. 0: east, 1: west: -1: unknown

    virtual TAlpacaErr siderealtime(float &v) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Returns the local apparent sidereal time.
    virtual TAlpacaErr get_utcdate(char *b) { b[0]= 0; return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Returns the UTC date/time of the telescope's internal clock. b will be at least 20chrs... "8910-91-19T25:83:67Z"
    virtual TAlpacaErr set_utcdate(char const *b) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Sets the UTC date/time of the telescope's internal clock.

    virtual TAlpacaErr axisrates(int axis, char *b) { b[0]= 0; return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Returns the rates at which the telescope may be moved about the specified $Axis  returns [{"Maximum": 0,"Minimum": 0}] in b (b will be 30 chr long)
    virtual bool canmoveaxis(int axis) { return false; } // Indicates whether the telescope can move the requested $Axis.
    virtual TAlpacaErr moveaxis(int axis, float rate) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Moves a telescope $Axis at the given $Rate.

    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// Series of functions that will look for the value for a given parameter an http form input...
// They differ by the type returned...
// note that if you want to use a string stuff directly, you better verify and sanitize it (there is a function for that, see lower)
char *getStrData(char *m, char const *parameter);
int getBoolData(char *m, char const *parameter); // return 0 or 1 (false/true) or 2 for neither if you care!
int getIntData(char *m, char const *parameter);
// format for text is: [-]AAA:mm:ss.sss with : that can be replaced by ' and "
char *floatToRa(float ra, char *b); // b must be long enough. b is returned...
static char inline *floatToDec(float dec, char *b) { return floatToRa(dec /**15.0f*/, b); } // b must be long enough. b is returned...
bool RaToFloat(char const *b, float &ra);  // return true if no error
bool DecToFloat(char const *b, float &ra); // return true if no error
