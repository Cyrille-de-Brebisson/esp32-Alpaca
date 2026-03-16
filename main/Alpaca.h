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
*     bool get_ismoving() override { return MotorFocus.isMoving(); }
*     int32_t get_maxstep() override { return MotorFocus.maxPos; }
*     int32_t get_position() override { return MotorFocus.pos; }
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
#define HASMilisecondTime // define this if you have a function called Milisecond which will return a time counter in milisecond. This is used for UTCTime in CTelescope.
#define ALPACA

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
    struct { char k[32]; uint8_t v[256]; } blobs[100]; int usedBlob= 0;
    void load(char const* fn); void save(char const* fn);
};
typedef int nvs_handle_t;
#endif

// This is a helper function doing the basic wifi start on esp systems given a network name, password (can be nullptr) and hostname.
// Typical use is as follow:
// char wifi[32], wifipass[32];
//  alpaca->load("wifi", "", wifi, sizeof(wifi));
//  alpaca->load("wifipass", "", wifipass, sizeof(wifipass));
//  startWifi(wifi, wifipass, alpaca->ServerName);
void startWifi(char const *net, char const *pass, char const *hostname);
extern bool volatile wificonnected; // indicate wifi connection status...
extern char ipadr[];       // if wificonnected, will have the IP address

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

    // call to the setup api. return false to have the system return err404. Else send what you want through sock and return true to continue the connection...
    // get is true if get, false if put. data is the data passed to the server
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
#ifndef _WIN32 // on win32, int and int32_t are the same and it causes conflicts
    void save(char const *key, int v) { save(key, int32_t(v)); }
#endif
    void save(char const *key, float v);
    void save(char const *key, uint8_t const *v, int size);
    // These are the same, but they handle a key spread on 2 strings. For example a "header" and "key"
    template <typename T> void save(char const *key1, char const *key2, T v) { char t[100]; strcpy(t, key1); strcat(t, key2); save(t, v); }
    void save(char const *key1, char const *key2, uint8_t const *v, int size)  { char t[100]; strcpy(t, key1); strcat(t, key2); save(t, v, size); }
    // all the loads have a default value returned if error...
    char *load(char const *key, char const *def, char *buf, size_t buflen); // put data in buf. return it also for convinience... buf MUST be large enough for def
    bool load(char const *key, uint8_t *buf, size_t buflen); // load data in buf. return true if successful
    int32_t load(char const *key, int32_t def);
#ifndef _WIN32
    int load(char const *key, int def) { return load(key, int32_t(def)); }
#endif
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

// Used by dispatch. This is a sting class that grows by packs of 1K... allows to not have any dependencies...
// string which is allocated by 1k blocks and has a printf... Used to contruct responses...
class CMyStr { public:
    char *c= nullptr; size_t csize= 0, w= 0;
    CMyStr() { c= (char*)malloc(csize= 1024); }
    ~CMyStr() { free(c); }
    void grow(int size= 2048) { c= (char*)realloc(c, csize= csize+size); }
    void append(char const *s, size_t l=-1)
    {
        if (l==-1) l=strlen(s);
        if (w+l>csize-1) grow();
        memcpy(c+w, s, l); w+= l;
    }
    CMyStr &operator +=(char const *s) { append(s); return *this; }
    void printf(const char* format, ...)
    {
        va_list args; 
        va_start(args, format); int size = vsnprintf(NULL, 0, format, args); va_end(args);
        if (csize-w<size+1) grow(size+1);
        va_start(args, format); vsprintf(c+w, format, args); va_end(args);
        w+= strlen(c+w);
    }
};

// copy s into d (size dlen) and make sure that there is a terminal 0 and no overwrite
static void strncpy2(char *d, char const *s, int dlen) { d[dlen-1]= 0; strncpy(d, s, dlen-1); }

// This is the generic Alpaca device class. All device types inherits from it...
class CAlpacaDevice { public:
    CAlpacaDevice(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription):id(id), driverInfo(driverInfo), driverVersion(driverVersion)
    { 
        keyHeader[0]= 0;
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
    char keyHeader[16];           // Will contain some text used to prefix any key for load/save... see load/save in addDevice and setup...
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
class CFocuser : public CAlpacaDevice { public: CFocuser(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { savedPos[0]=0; }
    uint32_t get_interfaceversion() override { return 4; }
    virtual bool get_absolute() { return true; }; // this means that the "move" is in steps, not in delta steps...
    virtual int32_t get_maxincrement() { return get_maxstep(); }; // This is typical of focusers...
    virtual TAlpacaErr put_move(int32_t position) = 0; // move to position, except if you change the get_absolute to false...
    virtual TAlpacaErr put_halt() = 0;  // Stop focusser
    virtual bool get_ismoving() = 0;    // return focuser moving state
    virtual int32_t get_position() = 0; // return current position. No clue what it means in non absolute mode!
        int32_t maxSteps= 65535;        // Change as you see fit! This is a default which can be changed and saved in flash through the setup UI or the ascom driver...
                                        // or you can override the function bellow...
    virtual int32_t get_maxstep() { return maxSteps; } // maximum value for position
        float stepSize= 10.0f;          // Change as you see fit! This is a default which can be changed and saved in flash through the setup UI or the ascom driver...
                                        // or you can override the function bellow...
    virtual float get_stepsize() { return stepSize; };   // size of a step in microns...
    // These are NOT ascom items, but since most stepper will need them, I have added them here and they are
    // configurable through the setup API. If you do NOT want them configurable, set them to -1 in your constructor...
    // for direction: 0 is direct, 1 is reverse and -1 is hide...
    int32_t maxSpeed= 200*4, msToMaxSpeed= 200, motorDirection= 0;
    // These are setters for motor config. override reinitMotor to do what you need when they are changed.
        virtual void reinitMotor() {}
    void set_stepSize(CAlpaca *Alpaca, float vf) { Alpaca->save(keyHeader, "stepSize", stepSize= vf); reinitMotor(); }
    void set_maxSteps(CAlpaca *Alpaca, int vi) { Alpaca->save(keyHeader, "MaxSteps", maxSteps= vi); reinitMotor(); }
    void set_maxSpeed(CAlpaca *Alpaca, int vi) { Alpaca->save(keyHeader, "maxSpeed", maxSpeed= vi); reinitMotor(); }
    void set_msToMaxSpeed(CAlpaca *Alpaca, int vi) { Alpaca->save(keyHeader, "msToMaxSpeed", msToMaxSpeed= vi); reinitMotor(); }
    void set_motorDirection(CAlpaca *Alpaca, int vi) { Alpaca->save(keyHeader, "motorDirection", motorDirection= vi); reinitMotor(); }
    // Saved position is a set of positions that can be saved.They are stored in a string of repeating pos_name value!
    // call getSavedPos to get that list. and the setSavedPos and eraseSavedPos to modify the list...
        char savedPos[1024]; bool savedPosLoaded= false;
    char *getSavedPos(CAlpaca *Alpaca) { if (savedPosLoaded) return savedPos; savedPosLoaded=true; Alpaca->load(keyHeader, "savedPos", "", savedPos, sizeof(savedPos)); return savedPos; }
    void eraseSavedPos(CAlpaca *Alpaca, char const *name);
    void setSavedPos(CAlpaca *Alpaca, char const *name);

        bool _tempComp= false;
    virtual bool get_tempcompavailable() { return false; }
    virtual TAlpacaErr get_tempcomp(bool *tempcomp) { *tempcomp= _tempComp; return ALPACA_OK; }
    virtual TAlpacaErr put_tempcomp(bool tempcomp) { if (tempcomp) return ALPACA_ERR_INVALID_VALUE; _tempComp= tempcomp; return ALPACA_OK; }
    virtual TAlpacaErr get_temperature(float *temperature) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    void subLoad(CAlpaca *alpaca) override;

    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
protected:
    char const *get_type() override { return "Focuser"; }
};

// This has never been tested and will not work as I have not written the dispatch and none of the features are implemented!
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

// This has never been tested and will not work as I have not written the dispatch nor the setup!
class CDome : public CAlpacaDevice { public: CDome(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 3; }

    // If you have a sliding roof, use this interface (shutter)...
    virtual TAlpacaErr get_cansetshutter(bool *cansetshutter) { *cansetshutter= false; return ALPACA_OK; }
      // ShutterState Open:0 Closed:1 Opening:2 Closing:3 Error:4
    virtual TAlpacaErr get_shutterstatus(int32_t *shutterstatus) { *shutterstatus= 4; return ALPACA_OK; }
    virtual TAlpacaErr put_closeshutter() { return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr put_openshutter() { return ALPACA_ERR_NOT_IMPLEMENTED; }

    // This part is mostly for rotating domes...
    bool slewing= false;
    virtual TAlpacaErr get_slewing(bool *slewing) { *slewing= this->slewing; return ALPACA_OK; }
    virtual TAlpacaErr put_abortslew()  { return ALPACA_ERR_NOT_IMPLEMENTED; }

    virtual TAlpacaErr get_canfindhome(bool *canfindhome) { *canfindhome= false; return ALPACA_OK; }
    bool athome= false;
    virtual TAlpacaErr get_athome(bool *athome) { *athome= this->athome; return ALPACA_OK; }
    virtual TAlpacaErr put_findhome() { return ALPACA_ERR_NOT_IMPLEMENTED; }

    float azimuth= 0.0f; // current value. Update when moves or override get
    virtual TAlpacaErr get_azimuth(float *azimuth) { *azimuth= this->azimuth; return ALPACA_OK; }
    virtual TAlpacaErr get_cansetazimuth(bool *cansetazimuth) { *cansetazimuth= false; return ALPACA_OK; }
    virtual TAlpacaErr put_slewtoazimuth(float azimuth)  { return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_cansyncazimuth(bool *cansyncazimuth) { *cansyncazimuth=false; return ALPACA_OK; }
    virtual TAlpacaErr put_synctoazimuth(float azimuth) { return ALPACA_ERR_NOT_IMPLEMENTED; }

    float altitude= 0.0f; // current value. Update when moves or override get
    virtual TAlpacaErr get_altitude(float *altitude) { *altitude= this->altitude; return ALPACA_OK; }
    virtual TAlpacaErr get_cansetaltitude(bool *cansetaltitude) { *cansetaltitude= cansetaltitude; return ALPACA_OK; }
    virtual TAlpacaErr put_slewtoaltitude(float altitude) { return ALPACA_ERR_NOT_IMPLEMENTED; }

    bool atpark= false;
    virtual TAlpacaErr get_atpark(bool *atpark) { *atpark= this->atpark; return ALPACA_OK; }
    virtual TAlpacaErr get_canpark(bool *canpark) { *canpark= false; return ALPACA_OK; }
    virtual TAlpacaErr put_park() { return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_cansetpark(bool *cansetpark) { *cansetpark=false; return ALPACA_OK; }
    virtual TAlpacaErr put_setpark() { return ALPACA_ERR_NOT_IMPLEMENTED; }

     // this is used for integrated systems where you have dome and telescope in the same system and they can coordonate...
    bool slaved= false;
    virtual TAlpacaErr get_canslave(bool *canslave) { *canslave= false; return ALPACA_OK; }
    virtual TAlpacaErr get_slaved(bool *slaved) { *slaved= this->slaved; return ALPACA_OK; }
    virtual TAlpacaErr put_slaved(bool slaved) { return ALPACA_ERR_NOT_IMPLEMENTED; }

protected:
    char const *get_type() override { return "Dome"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested and will not work as I have not written the dispatch!
class CObservingConditions : public CAlpacaDevice { public: CObservingConditions(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 2; }
    virtual TAlpacaErr get_averageperiod(float *averageperiod) { *averageperiod= this->averageperiod; return ALPACA_OK; }
    virtual TAlpacaErr put_averageperiod(float averageperiod) { this->averageperiod= averageperiod;  return ALPACA_OK; }
    virtual TAlpacaErr get_cloudcover(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_dewpoint(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_humidity(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_pressure(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_rainrate(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_skybrightness(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_skyquality(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_skytemperature(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_starfwhm(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_temperature(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_winddirection(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_windgust(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_windspeed(float *v) { *v= 0.0f; return ALPACA_ERR_NOT_IMPLEMENTED; }
    virtual TAlpacaErr put_refresh() = 0;
    virtual TAlpacaErr get_sensordescription(char const *sensorname, char const *&buf) = 0; // return a pointer to the description for the requested sensor... Typically the specs...
    virtual TAlpacaErr get_timesincelastupdate(float *timesincelastupdate) = 0;
    protected:
        float averageperiod= 1.0f/24.0f/60.0f; // default is 1 minute..
    char const *get_type() override { return "ObservingConditions"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested and is only partially implemented (no setup)
class CRotator : public CAlpacaDevice { public: CRotator(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 4; }
    // Assumes that there is a mechanical position and will handle relative position as an offset to it for you...
    // So you only need to implement these 5 functions...
    virtual TAlpacaErr get_mechanicalposition(float *mechanicalposition) = 0;
    virtual TAlpacaErr put_movemechanical(float position) = 0;
    virtual TAlpacaErr get_stepsize(float *stepsize) = 0;
    virtual TAlpacaErr get_ismoving(bool *ismoving) = 0;
    virtual TAlpacaErr put_halt() = 0;
    // handle relative to absolue for you!
    virtual TAlpacaErr get_position(float *position) { TAlpacaErr er= get_mechanicalposition(position); if (er==ALPACA_OK) *position+= offset; return er; }
    virtual TAlpacaErr put_sync(float position) { float pos; get_mechanicalposition(&pos); offset= position-pos; return ALPACA_OK; }
    virtual TAlpacaErr get_targetposition(float *targetposition) { *targetposition= this->targetposition; return ALPACA_OK;}
    virtual TAlpacaErr put_move(float position) { float pos; get_position(&pos); return put_moveabsolute(pos+position); }
    virtual TAlpacaErr put_moveabsolute(float position) { return put_movemechanical(targetposition=position-offset); }
    // Assumes that you can reverse and will handle saving this for you
    virtual TAlpacaErr get_canreverse(bool *canreverse) { *canreverse= true; return ALPACA_OK; }
    virtual TAlpacaErr get_reverse(bool *reverse) { bool can; get_canreverse(&can); if (!can) return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; *reverse= this->reverse; return ALPACA_OK; }
    virtual TAlpacaErr put_reverse(bool reverse)  { bool can; get_canreverse(&can); if (!can) return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; this->reverse= reverse; return ALPACA_OK; }
protected:
    bool reverse= false;
    float offset= 0.0f, targetposition= 0.0f;
    char const *get_type() override { return "Rotator"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    //void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested and is only partially implemented (no setup)
class CSafetyMonitor : public CAlpacaDevice { public: CSafetyMonitor(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 3; }
    virtual TAlpacaErr get_issafe(bool *issafe) = 0;
protected:
    char const *get_type() override { return "SafetyMonitor"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    //void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This has never been tested and is only partially implemented (no setup). Should you need it, contact me as it will not be long to write!
class CSwitch : public CAlpacaDevice { public: CSwitch(int id, char const *driverInfo, char const *driverVersion, char const *defaultName, char const *defaultDescription): CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
    uint32_t get_interfaceversion() override { return 3; }
    // Let us assume that you want to use default helpers...
    // override the defaultSwitchDefs function to return the default switches description.
    // then override the put_setswitch and put_setswitchvalue to act on the changes...
    // you might also want to work on the subInit to change the initialization value if not equal to the minswitchvalues...
	// The default implementation only requires you to implement defaultSwitchDefs and switchChanged
	// and lets the user change/save switches names and description...
      struct TSwitchDef { bool canwrite, canasync; char const*switchname, *switchdescription; float switchstep=1.0f, minswitchvalue=0.0f, maxswitchvalue=1.0f; };
    virtual TSwitchDef const *defaultSwitchDefs(int32_t &nb) =0; // { nb= 0; return nullptr; }

    virtual TAlpacaErr get_maxswitch(int32_t *maxswitch) { init(0); *maxswitch= nbswitches; return ALPACA_OK; }
    // get info on the various switches... Most is goten from the struct returned by the defaultSwitchDefs function. But names and description can be overriden by the user...
    virtual TAlpacaErr get_canwrite(int32_t id, bool *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].canwrite; return ALPACA_OK; }
    virtual TAlpacaErr get_getswitchdescription(int32_t id, char *buf, size_t len) 
    { 
        if (!init(id)) return ALPACA_ERR_INVALID_VALUE; 
        strncpy2(buf, switches[id].switchdescription, int(len)); 
        char t[20]; sprintf(t, "%ldName", id); alpaca->load(keyHeader, t, buf, buf, len);
        return ALPACA_OK; 
    }
    virtual TAlpacaErr get_getswitchname(int32_t id, char *buf, size_t len) 
    { 
        if (!init(id)) return ALPACA_ERR_INVALID_VALUE; 
        strncpy2(buf, switches[id].switchdescription, int(len));
        char t[20]; sprintf(t, "%ldDesc", id); alpaca->load(keyHeader, t, buf, buf, len);
        return ALPACA_OK; 
    }
    virtual TAlpacaErr put_setswitchname(int32_t id, const char *name) { char t[20]; sprintf(t, "%ldName", id); alpaca->save(keyHeader, t, name); return ALPACA_OK; }
    virtual TAlpacaErr put_setswitchdescription(int32_t id, const char *name) { char t[20]; sprintf(t, "%ldDesc", id); alpaca->save(keyHeader, t, name); return ALPACA_OK; }
    // get/set bool value
    virtual TAlpacaErr get_getswitch(int32_t id, bool *value)  { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switchesValues[id]!=0; return ALPACA_OK; }
    virtual TAlpacaErr put_setswitch(int32_t id, bool value) 
    { 
        float m, M; if (get_minswitchvalue(id, &m)!=ALPACA_OK) return ALPACA_ERR_INVALID_VALUE; get_maxswitchvalue(id, &M);
        switchChanged(id, switchesValues[id]= value?M:m);
        return ALPACA_OK; 
    }
    // handle variable switches
    virtual TAlpacaErr get_switchstep(int32_t id, float *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].switchstep; return ALPACA_OK; }
    virtual TAlpacaErr get_minswitchvalue(int32_t id, float *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].minswitchvalue; return ALPACA_OK; }
    virtual TAlpacaErr get_maxswitchvalue(int32_t id, float *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].maxswitchvalue; return ALPACA_OK; }
    virtual TAlpacaErr get_getswitchvalue(int32_t id, float *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switchesValues[id]; return ALPACA_OK; }
    virtual TAlpacaErr put_setswitchvalue(int32_t id, float value) 
    { 
        if (!init(id)) return ALPACA_ERR_INVALID_VALUE; 
        float m, M; get_minswitchvalue(id, &m); get_maxswitchvalue(id, &M);
        if (value<m || value>M) return ALPACA_ERR_INVALID_VALUE;
        switchesValues[id]= value;
        switchChanged(id, value);
        return ALPACA_OK; 
    }
    // async operations... These are not implemented at all at this point. If you need them... contact me :-)
    virtual TAlpacaErr get_canasync(int32_t id, bool *value) { if (!init(id)) return ALPACA_ERR_INVALID_VALUE; *value= switches[id].canasync; return ALPACA_OK; }
    virtual TAlpacaErr put_setasync(int32_t id, bool state) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual TAlpacaErr put_setasyncvalue(int32_t id, float value) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }
    virtual TAlpacaErr get_statechangecomplete(int32_t id, bool *completed) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; }

protected:
    int32_t nbswitches=0; TSwitchDef const *switches= nullptr;
    float *switchesValues= nullptr;
    virtual void switchChanged(int32_t id, float value) = 0; // override this to act on switch value changed. value is min/max switch values if the setswitch(bool) function is used.
    virtual void subInit() {} // override to do something when the default values are read. For example you can use that to change the current switches values at startup...
    bool init(int id) // makes sure that everything is initialized properly and return true if id is a valid switch id!
    {
        if (nbswitches==0)
        {
            switches= defaultSwitchDefs(nbswitches);
            switchesValues= (float*)malloc(nbswitches*sizeof(float));
            for (int i=0; i<nbswitches; i++) switchesValues[i]= switches[i].minswitchvalue;
            subInit(); // allows configuraiton...
        }
        return id>=0 && id<nbswitches;
    }
    char const *get_type() override { return "Switch"; }
    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
    // void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
};

// This been tested and works :-) for once!
class CTelescope : public CAlpacaDevice { 
    public: CTelescope(int id, char const* driverInfo, char const* driverVersion, char const* defaultName, char const* defaultDescription) : CAlpacaDevice(id, driverInfo, driverVersion, defaultName, defaultDescription) { }
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
    virtual TAlpacaErr set_siteelevation(float v) { if (v<-200.0f || v>9999.0f) return ALPACA_ERR_INVALID_VALUE; alpaca->save(keyHeader, "set_siteelevation", v); return ALPACA_OK; } // Sets the observing site's elevation above mean sea level.
    virtual float get_sitelatitude() { return alpaca->load(keyHeader, "sitelatitude", 45.007109f); } // Returns the observing $SiteLatitude .
    virtual TAlpacaErr set_sitelatitude(float v) { if (v<-90.0f || v>90.0f) return ALPACA_ERR_INVALID_VALUE; alpaca->save(keyHeader, "sitelatitude", v); return ALPACA_OK; } // Sets the observing site's latitude.
    virtual float get_sitelongitude() { return alpaca->load(keyHeader, "sitelongitude", 4.335247f); } // Returns the observing site's longitude.
    virtual TAlpacaErr set_sitelongitude(float v) {  if (v<-180.0f || v>180.0f) return ALPACA_ERR_INVALID_VALUE;alpaca->save(keyHeader, "sitelongitude", v); return ALPACA_OK; } // Sets the observing $SiteLongitude .

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
    virtual float get_guideratedeclination() { if (guideratedeclination<0) guideratedeclination= alpaca->load(keyHeader, "guideratedeclination", 9.0f/3600.0f); return guideratedeclination; }  // Returns the current Declination rate offset for telescope guiding
    virtual TAlpacaErr set_guideratedeclination(float v)  { guideratedeclination= v; alpaca->save(keyHeader, "guideratedeclination", guideratedeclination); return ALPACA_OK; } // Sets the current $GuideRateDeclination rate offset for telescope guiding.
    virtual float get_guideraterightascension()  { if (guideraterightascension<0) guideraterightascension= alpaca->load(keyHeader, "guideraterightascension", 9.0f/3600.0f); return guideraterightascension; } // Returns the current RightAscension rate offset for telescope guiding
    virtual TAlpacaErr set_guideraterightascension(float v) { guideraterightascension= v; alpaca->save(keyHeader, "guideraterightascension", guideraterightascension); return ALPACA_OK; } // Sets the current $GuideRateRightAscension  rate offset for telescope guiding.

    virtual bool cansettracking() { return true; } // Indicates whether the Tracking property can be changed.
        bool tracking= true;
    virtual bool get_tracking() { return tracking; } // Indicates whether the telescope is tracking.
    virtual TAlpacaErr set_tracking(bool v) { tracking= v; return ALPACA_OK; } // Enables or disables telescope $Tracking.
        int trackingrate= 0; // 0: sideral, 1: lunar, 2: solar, 3: king (15.0369 arc"/s)
    virtual int get_trackingrate() { return trackingrate; } // Returns the current tracking rate. 0: sideral, 1: lunar, 2: solar, 3: king (15.0369 arc"/s)
    virtual TAlpacaErr set_trackingrate(int v) { if (v<0 || v>3) return ALPACA_ERR_INVALID_VALUE; trackingrate= v; return ALPACA_OK; } // Sets the mount's $TrackingRate.
    virtual char const *trackingrates() { return "[0,1,2,3]"; } // Returns a collection of supported DriveRates values. i.e: a json object that has 0 to 4 integers in it...

    virtual bool cansetrightascensionrate() { return false; } // Indicates whether the RightAscensionRate property can be changed.
    virtual float get_rightascensionrate() { return 0.0f; } // Returns the telescope's right ascension tracking rate.
    virtual TAlpacaErr set_rightascensionrate(float v) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Sets the telescope's $RightAscensionRate tracking rate.
    virtual bool cansetdeclinationrate() { return false; } // Indicates whether the DeclinationRate property can be changed.
    virtual float get_declinationrate() { return 0.0f; } // Returns the telescope's declination tracking rate.
    virtual TAlpacaErr set_declinationrate(float v) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Sets the telescope's $DeclinationRate tracking rate.
         
    virtual bool slewing() = 0; // Indicates whether the telescope is currently slewing.
    virtual TAlpacaErr set_slewsettletime(int32_t v) { if (v<0) return ALPACA_ERR_INVALID_VALUE; alpaca->save(keyHeader, "slewsettletime", v); return ALPACA_OK; }// Sets the post-slew $SlewSettleTime time.
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
    virtual TAlpacaErr set_targetdeclination(float v) { if (v<-0.0f || v>90.0f) return ALPACA_ERR_INVALID_VALUE; targetdeclination= v; return ALPACA_OK; } // Sets the $TargetDeclination of a slew or sync.
    virtual float get_targetrightascension() { return targetrightascension; } // Returns the current target right ascension.
    virtual TAlpacaErr set_targetrightascension(float v) {  if (v<-0.0f || v>24.0f) return ALPACA_ERR_INVALID_VALUE; targetrightascension= v; return ALPACA_OK; } // Sets the target $TargetRightAscension of a slew or sync.
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
    #ifndef HASMilisecondTime // These are concidered mandatory by Ascom
        virtual TAlpacaErr get_utcdate(char *b) =0; // Returns the UTC date/time of the telescope's internal clock. b will be at least 20chrs... "8910-91-19T25:83:67Z"
        virtual TAlpacaErr set_utcdate(char const *b) =0; // Sets the UTC date/time of the telescope's internal clock.
    #else
        virtual TAlpacaErr get_utcdate(char* b);
        virtual TAlpacaErr set_utcdate(char const* b);
    #endif

    virtual TAlpacaErr axisrates(int axis, char *b) { b[0]= 0; return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Returns the rates at which the telescope may be moved about the specified $Axis  returns [{"Maximum": 0,"Minimum": 0}] in b (b will be 30 chr long)
    virtual bool canmoveaxis(int axis) { return axis<2; } // Indicates whether the telescope can move the requested $Axis.
    virtual TAlpacaErr moveaxis(int axis, float rate) { return ALPACA_ERR_ACTION_NOT_IMPLEMENTED; } // Moves a telescope $Axis at the given $Rate.

    bool dispatch(bool get, char const *url, char *m, CMyStr *s) override;
        struct {
            struct { int maxPos= 200*256*130, maxSpd= 200*256*2, msToSpd= 200, Backlash= 0, invert= 0; } ra, dec;
            int raSettle= 0, raAmplitude=180+15;
        } mount;
        virtual void doReinit() {} // override to do things when motor settings get changed...
    void subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s) override; // This allows you to add stuff in the HTML or handle inputs...
    void subLoad(CAlpaca *alpaca);
};

// Series of functions that will look for the value for a given parameter an http form input...
// They differ by the type returned...
// note that if you want to use a string stuff directly, you better verify and sanitize it (there is a function for that, see lower)
char *getStrData(char *m, char const *parameter);
int getBoolData(char *m, char const *parameter); // return 0 or 1 (false/true) or 2 for neither if you care!
int getIntData(char *m, char const *parameter);
bool getIntData(char *m, char const *parameter, int &v);
bool getFloatData(char *m, char const *parameter, float &v);
int getIntDataDef(char *m, char const *parameter, int def);
float getFloatDataDef(char *m, char const *parameter, float def);
// format for text is: [-]AAA:mm:ss.sss with : that can be replaced by ' and "
char *floatToRa(float ra, char *b); // b must be long enough. b is returned...
static char inline *floatToDec(float dec, char *b) { return floatToRa(dec /**15.0f*/, b); } // b must be long enough. b is returned...
bool RaToFloat(char const *b, float &ra);  // return true if no error
bool DecToFloat(char const *b, float &ra); // return true if no error
char *getHtmlString(char const *in, char *buf, size_t buflen); // html string decyphering...
