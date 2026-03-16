// Of interest: ascom pdf and web versions of API descriptions...
// https://ascom-standards.org/AlpacaDeveloper/ASCOMAlpacaAPIReference.html#[11,%22XYZ%22,69,495,null]
// https://ascom-standards.org/api/?urls.primaryName=ASCOM%20Alpaca%20Management%20API#/
// https://ascom-standards.org/api/#/


////////////////////////////////////////////////////////////////////////////
// Alpaca API system.
// Alpaca "hubs" listen to a UDP port.
// Alpaca application broadcast a "hey" on said port and "hub" reply with the port used by the hub http connection
// Alpaca application will then use http protocol to access the following urls on the hub:
// 
// GET /management/apiversions
// GET /management/v1/description
// GET /management/v1/configureddevices
// GET /setup
// GET /setup/v1/focuser/0/setup
// GET /api/v1/focuser/0/command?parameters=...&parameters=...&
// PUT /api/v1/focuser/0/command with the parameters=...&parameters=...& under as "data"....
// 
// ---------------------
// Typical HTTP response:
// HTTP/1.1 200 OK
// Content-Type: application/json; charset=utf-8
// Transfer-Encoding: chunked
// 
// 5b8
// {"Value":[{"DeviceName":"Alpaca Camera Sim","DeviceType":"Camera","DeviceNumber":0,"UniqueID":"295cd610-dd10-48bc-a2aa-3e3c8a9cf95c"},{"DeviceName":"Alpaca CoverCalibrator Simulator","DeviceType":"CoverCalibrator","DeviceNumber":0,"UniqueID":"7c53db45-11ce-43ce-aa24-43a158c38489"},{"DeviceName":"Alpaca Dome Simulator","DeviceType":"Dome","DeviceNumber":0,"UniqueID":"88da0d0e-f36d-47bc-9382-f2b55d442300"},{"DeviceName":"Alpaca Filter Wheel Simulator","DeviceType":"FilterWheel","DeviceNumber":0,"UniqueID":"a48deb53-ac03-4dd9-bbf8-45f78d99c082"},{"DeviceName":"Alpaca Focuser Simulator","DeviceType":"Focuser","DeviceNumber":0,"UniqueID":"d1a1068f-f5ba-477c-bf7b-dbfc132c9c87"},{"DeviceName":"Alpaca Observing Conditions Simulator","DeviceType":"ObservingConditions","DeviceNumber":0,"UniqueID":"774623d2-4202-4803-ab30-e79e5b12a785"},{"DeviceName":"Alpaca Rotator Simulator","DeviceType":"Rotator","DeviceNumber":0,"UniqueID":"750dccec-e7b6-4d4f-a452-775250a91240"},{"DeviceName":"Alpaca Safety Monitor Simulator","DeviceType":"SafetyMonitor","DeviceNumber":0,"UniqueID":"74af2f9f-b76e-4f67-be5f-a192af65ef41"},{"DeviceName":"Alpaca Switch Simulator","DeviceType":"Switch","DeviceNumber":0,"UniqueID":"92c294ed-7fbd-4856-899a-eca52291c9bd"},{"DeviceName":"Alpaca Telescope Simulator","DeviceType":"Telescope","DeviceNumber":0,"UniqueID":"7a5bc92c-3f0d-4f29-b5a2-0ea93b83e6a0"}],"ClientTransactionID":0,"ServerTransactionID":8,"ErrorNumber":0,"ErrorMessage":""}
// 0
//
// -------------------
// Typical PUT API data... Seems that the standard is xform encoded, NOT jason!!!
// PUT /api/v1/focuser/0/connected HTTP/1.1
// Connection: keep-alive
// Content-Type: application/x-www-form-urlencoded
// Content-Length: 48
// 
// Connected=False&ClientID=1&ClientTransactionID=1
// -------------------
// Typical GET API data...
// GET /api/v1/focuser/0/connected?ClientID=1&ClientTransactionID=1 HTTP/1.1
// Connection: keep-alive
// Content-Type: application/x-www-form-urlencoded
// 
//





#include "Alpaca.h"
#include <atomic>
#include <math.h>

#ifndef _WIN32
    #include <esp_mac.h>
    #include <esp_err.h>
    #include <freertos/FreeRTOS.h>
    #include <lwip/sockets.h>
    #include <lwip/netdb.h>
    #include <esp_log.h>
    void CAlpaca::osinit() 
    { // Generates a uniqeID used for devices identification...
        uint8_t mac[8];
        esp_efuse_mac_get_default(mac);
        for (int i=0; i<6; i++) uniqueid[i*2]= 'a'+(mac[i]&0x15), uniqueid[i*2+1]= 'a'+((mac[i]>>4)&0x15);
    }
    typedef int SOCKET;
    static int const INVALID_SOCKET= -1;
    #define packdebug(...) // define to print request debugs...
    static void inline _itoa(int v, char *b, int radix) { itoa(v, b, radix); }
    #if 1 // change if you do, or do not have issues with this...
      extern "C" int lwip_hook_ip6_input(struct pbuf *p, struct netif *inp) __attribute__((weak));
      extern "C" int lwip_hook_ip6_input(struct pbuf *p, struct netif *inp) { if (ip6_addr_isany_val(inp->ip6_addr[0].u_addr.ip6)) { pbuf_free(p); return 1; } return 0; }
    #endif
      
#else
    // This is a bunch of code that will allow you to compile under windows... Which can make testing a WHOLE lot easier!!!!
    #define LOCALHOSTONLY // define this to limit UDP listening to localhost! avoids getting messages from multiple routes...
    #include <WinSock2.h>
    #pragma comment(lib, "WS2_32.lib")
    #include <ws2tcpip.h>
    void CAlpaca::osinit() 
    { 
        WSADATA wsaData;
        int iResult = WSAStartup(MAKEWORD(2,2), &wsaData); if (iResult != 0) 
        printf("WSAStartup failed with error: %d\n", iResult); 
        strcpy(uniqueid, "234564321234");
    }
    #include <stdint.h>
    #include <stdio.h>
    typedef unsigned int uint;
    #define ESP_LOGE(A, ...) { printf(__VA_ARGS__); printf("\r\n"); }
    #define ESP_LOGD(A, ...) { printf(__VA_ARGS__); printf("\r\n"); }
    #define ESP_LOGI(A, ...) { printf(__VA_ARGS__); printf("\r\n"); }
    static void vTaskDelay(int a) { Sleep(a); }
    typedef int socklen_t;
    static void close(SOCKET) {}
    void xTaskCreate(void (*cb)(void*), char const *, int stack, void *p, int, void*) { CreateThread(0, stack, (LPTHREAD_START_ROUTINE)cb, p, 0, nullptr); }
    void vTaskDelete(void *){}
    #define packdebug(...) printf("%s", __VA_ARGS__) // define to print request debugs...
#endif

// returns true if s1 starts with s2. assumes s1 is lowercase, but s2 migth not be...
static bool startsWithNonCase(char const *s1, char const *s2)
{
    while (true)
    {
        char c2= *s2++; if (c2==0) return true; if (c2>='A' && c2<='Z') c2+= 'a'-'A';
        char c1= *s1++; if (c1>='A' && c1<='Z') c1+= 'a'-'A';
        if (c1!=c2) return false;
    }
}
// read a positive int from s, and moves s to the end of the int.... If no int, returns 0...
template <typename T>
static int readInt(T *&s) 
{
    bool neg= false;
    if (*s=='-') neg= true, s++;
    int res= 0;
    while (*s>='0' && *s<='9') res= res*10+*s++-'0';
    return neg?-res:res;
}
// read a positive int from s, and moves s to the end of the int.... return false if there is no number to read
static bool readInt(char const *&b, int &v)
{
    v= 0;
    if (*b<'0' || *b>'9') return false;
    v= readInt<>(b);
    return true;
}
// read a positive float from s, and moves s to the end of the float.... If no float, returns 0...
template <typename T>
static float readFloat(T *&s)
{
    float r= float(readInt(s));
    if (*s!='.') return r; s++;
    float div= r<0.0f ? -0.1f : 0.1f;
    while (*s>='0' && *s<='9') { r+= div*(*s++-'0'); div/=10.0f; }
    return r;
}

static const char *TAG = "Alpaca"; // For debug on ESP32...

// This listen to UDP discovery requests and replies by saying "I am here"!!!
// port is the http port for doing actual work passed as a parameter
static void udp_server_task(void *port)
{
  char response[64]; sprintf(response, "{\"AlpacaPort\":%d}", (int)(size_t)port); // prep response packets (always the same)
  size_t const response_len = strlen(response);

  while (true)
  {
    // create udp socket...
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) { ESP_LOGE(TAG, "UDP Unable to create  socket: errno %d", errno); vTaskDelay(1000); continue; }
    ESP_LOGI(TAG, "UDP listener Socket created");
    // make it listen to broadcasts...
    struct sockaddr_in dest_addr; memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(32227);  // By Alpaca definition!
    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) { ESP_LOGE(TAG, "UDP Socket unable to bind: errno %d", errno); vTaskDelay(1000); continue; }
    ESP_LOGI(TAG, "UDP Socket bound, port %d", htons(dest_addr.sin_port));

    while (true)
    {
      char rx_buffer[65]; // 64 by alpaca standard plus 1 for a final 0 if/as needed
      struct sockaddr_storage source_addr; socklen_t socklen= sizeof(source_addr); // Large enough for both IPv4 or IPv6
      // get data from anyone...
      int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer)-1, 0, (struct sockaddr *)&source_addr, &socklen);
      if (len < 0) { ESP_LOGE(TAG, "UDP recvfrom failed: errno %d len:%d", errno, len); break; }
      rx_buffer[len]=0;
      ESP_LOGI(TAG, "UDP received %d %s", len, rx_buffer);
      // Data received
#ifdef LOCALHOSTONLY // for local testing, this only gets messages from localhost for easier testing...
      if (source_addr.__ss_pad1[2]!=127) continue;
#endif
      if (len>=16 && memcmp(rx_buffer, "alpacadiscovery1", 16)==0) // known discovery message?
      {
        err = sendto(sock, response, (int)response_len, 0, (sockaddr*)&source_addr, sizeof(source_addr)); // reply
        if (err < 0) { ESP_LOGE(TAG, "UDP Socket unable to sendto: errno %d", errno); continue; }
        ESP_LOGI(TAG, "UDP sent %s", response);
      }
    }

    ESP_LOGI(TAG, "UDP Shutting down socket and restarting..."); 
    shutdown(sock, 0); close(sock);
  }
}

// This is task that handles ONE HTTP client. Sock is open through an accept and passed through the alpaca object...
// This function received a http request, finds if it's a GET or PUT, find the url and data before calling alpaca to handle it...
static void HTTPClient(void *P)
{
    CAlpaca *alpaca= (CAlpaca *)P;
    SOCKET sock= alpaca->newClientSocket; alpaca->newClientSocket= -1; // get sock
    #ifndef _WIN32
    timeval timeout= { .tv_sec=120, .tv_usec=0 }; setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout); // 2 minutes timeout on sock receve (the system is different on windows... but not needed for test, so... not there)
    #endif
    ESP_LOGI(TAG, "HTTP new client");
    int keepAlive= 1; setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&keepAlive, sizeof(int)); // Set tcp keepalive option. I don't honneslty know what that means!!!
    char buf[1024]; size_t bs= 0; // buffer and buffer size./ max 1024 bytes read.. Assumes all ascom messages are smaller... Will need to check!
    while (true)
    {
        int rlen= recv(sock, buf+bs, (int)(sizeof(buf)-bs), 0); // receive data. rlen is received len or error
        if (rlen<=0) { ESP_LOGE(TAG, "HTTP recv failed: %d", rlen); break; }
        bs+= rlen;
        if (bs>=sizeof(buf)-1) break; // We have a problem here! stop...
        if (bs<4) continue;           // not enough data for anything...
        bool get= true;               // by default in get mode
        buf[bs]= 0; char *end, *data; // null terminate to use str functions, end will be end of packet, data will be data part..
        if (memcmp(buf, "GET ", 4)==0) // Get case.
        {   
            // The line of interest will look like the comment bellow and will end with a double cr/ln...
            // GET /api/v1/focuser/0/connected?ClientID=1&ClientTransactionID=1 HTTP/1.1
            // Here we are intereted in the URL and what is after the '?'
            end= strstr(buf, "\r\n\r\n"); if (end==nullptr) continue; end+= 4; // we don't have the end yet
            // find a '?' or a space. If '?', replace by ' ' and return the char after (first char of data).. else, no data...
            data= buf+4; while (true) if (*data=='?') { *data++= ' '; break; } else if (*data<=' ') { data= nullptr; break; } else data++; 
        }
        else if (memcmp(buf, "PUT ", 4)==0) // PUT type. data is at the end of http request...
        { 
            // Typical PUT request: We are interested inthe URL and the "content"
            // PUT /api/v1/focuser/0/connected HTTP/1.1
            // Connection: keep-alive
            // Content-Type: application/x-www-form-urlencoded
            // Content-Length: 48
            // 
            // Connected=False&ClientID=1&ClientTransactionID=1
            get= false;
            end= strstr(buf, "Content-Length:"); if (end==nullptr) continue; // find length of data marker
            end+= 15; while (*end==' ' || *end=='\t') end++; // skip lengh marker
            int l= readInt(end);                             // get length
            end= strstr(end, "\r\n\r\n"); if (end==nullptr) continue; end+=4; // find end of header
            data= end; end+= l;                     // end of header is start of data. end of buffer is l bytes later...
            if (end>buf+bs) continue;               // if we don't have all the data... wait some more...
        }
        else { char const er[]= "HTTP/1.0 400 Bad Request\r\n\r\n"; send(sock, er, sizeof(er)-1, 0); break; } // error...

        // packdebug(buf);
        char savedChar, *savedCharPos= nullptr; // save a character that we will change and that might be the first char of the next message!
        if (data!=nullptr) // 0 terminate data. Since data CAN be at end of message, save what was under the 0 in case it was important...
        {
            char *t= data; while (t<end && *t>' ') t++; // find a separator or the end of the message...
            savedCharPos= t; savedChar= *savedCharPos; *savedCharPos= 0; // save it and put a 0!
        }
        { char *t= strchr(buf+4, ' '); if (t!=nullptr) *t= 0; } // 0 terminate the URL... no need to save the char there as URL can NOT be at end of message...
        ESP_LOGI(TAG, "HTTP new http %s %s", buf, data!=nullptr?data:"");
        packdebug(buf); packdebug("\r\n"); if (data) { packdebug(data); packdebug("\r\n"); }
        packdebug("\r\n<-----------------------------------\r\n");
        if (!alpaca->execRequest(sock, get, buf+4, data)) break; // Have alpaca handle the request! Do the actual work...
        packdebug("\r\n-----------------------------------\r\n");
        // Assumes keep alive... client can close if it wants!!!!
        if (savedCharPos) *savedCharPos= savedChar;  // restore removed char
        bs-= end-buf; memcpy(buf, end, bs);          // and shift buffer down for next message...
    }
    ESP_LOGI(TAG, "HTTP close");
    shutdown(sock, 0); close(sock); // cleanup
    vTaskDelete(nullptr);
}

// This is the HTTP server task. listen on the given port and accept clients.
// starts a new task for each of them...
static void HTTPTask(void *P)
{
    CAlpaca *alpaca= (CAlpaca *)P;
    SOCKET ListenSocket; int iResult;

    ESP_LOGI(TAG, "HTTPServer start %d", alpaca->httpport);

    // binding address
    struct sockaddr_storage dest_addr; struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(alpaca->httpport);

    // Create a SOCKET for the server to listen for client connections.
    ListenSocket= socket(dest_addr_ip4->sin_family, SOCK_STREAM, IPPROTO_IP);
    if (ListenSocket==INVALID_SOCKET) { ESP_LOGE(TAG, "HTTPServer socket failed: %d", errno); goto er; }
    #ifndef _WIN32
        { int opt= 1; setsockopt(ListenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); }
    #endif
    // setsockopt(ListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
    // Setup the TCP listening socket
    iResult = bind(ListenSocket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (iResult<0) { ESP_LOGE(TAG, "HTTPServer bind failed: %d", errno); goto er; }
    alpaca->newClientSocket= -1;

    //Now, await clients...
    while (true)
    {
        ESP_LOGI(TAG, "HTTPServer listen");
        iResult = listen(ListenSocket, 5);
        if (iResult<0) { ESP_LOGE(TAG, "HTTPServer listen failed: %d", errno); goto er; }
        // Accept a client socket
        SOCKET ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) { ESP_LOGE(TAG, "HTTPServer accept failed: %d", errno); goto er; }
        while (alpaca->newClientSocket!=-1) vTaskDelay(1); // will be set to -1 by the HTTPClient as soon as it picks it! which should be almost instantaneous...
        // pass the ClientSocket to HTTPClient and run it...
        alpaca->newClientSocket= ClientSocket; 
        ESP_LOGI(TAG, "HTTPServer accepted %d", alpaca->newClientSocket);
        xTaskCreate(HTTPClient, "http_client", 8192, alpaca, 2, nullptr);
    }
    er:
    ESP_LOGE(TAG, "HTTPServer stopped");
    vTaskDelete(nullptr);
}

// start the Alpaca services. UDP announcer and TCP listener...
void CAlpaca::start(int port)
{
    osinit(); // unique identificator plus other stuff as needed...
    httpport= port;
    xTaskCreate(udp_server_task, "udp_server_task", 2048, (void*)(size_t)port, 2, nullptr);
    xTaskCreate(HTTPTask, "http_server_task", 2048, this, 2, nullptr);
}

// Initiationation. Gets init values from code or storage...
CAlpaca::CAlpaca(char const *Manufacturer, char const *ManufacturerVersion, char const *DefaultServerName, char const *DefaultLocation) 
{
    strncpy2(this->ServerName, DefaultServerName ,sizeof(this->ServerName));
    strncpy2(this->wifi, DefaultServerName ,sizeof(this->wifi));
    wifip[0]= 0;
    strncpy2(this->Manufacturer, Manufacturer ,sizeof(this->Manufacturer));
    strncpy2(this->ManufacturerVersion, ManufacturerVersion ,sizeof(this->ManufacturerVersion));
    strncpy2(this->Location, DefaultLocation , sizeof(this->Location));
    uniqueid[0]= 0; 
    saveLoadBegin();
    char t[sizeof(this->Location)];
    strcpy(this->ServerName, load("ServerName", this->ServerName, t, sizeof(this->ServerName)));
    strcpy(this->Location, load("Location", this->Location, t, sizeof(this->Location)));
    strcpy(this->wifi, load("wifi", "", t, sizeof(this->wifi)));
    strcpy(this->wifip, load("wifip", "", t, sizeof(this->wifip)));
    saveLoadEnd();
}

// Adds a new device to the device list...
// Load it's user name, description and generate often used stuff..
void CAlpaca::addDevice(CAlpacaDevice *a)
{ 
    if (nbDevices==maxDevices) return;
    a->alpaca= this;
    memcpy(a->keyHeader, a->get_type(), 2); _itoa(a->id, a->keyHeader+2, 10); size_t tl= strlen(a->keyHeader); // create the "start of the handle" as type/id
    saveLoadBegin();
    load(a->keyHeader, "Name", a->Name, a->Name, sizeof(a->Name));
    load(a->keyHeader, "Description", a->Description, a->Description, sizeof(a->Description));
    a->subLoad(this);
    saveLoadEnd();
    // generate json
    snprintf(a->_jsonDesc, sizeof(a->_jsonDesc)-1, "{\"DeviceName\":\"%s\",\"DeviceType\":\"%s\",\"DeviceNumber\":%d,\"UniqueID\":\"%s%s\"},", a->get_name(), a->get_type(), a->id, uniqueid, a->keyHeader);
    // and finally, add...
    devices[nbDevices++]= a; 
}

// device type specific initialization...
void CFilterWheel::subLoad(CAlpaca *alpaca)
{
    alpaca->load(keyHeader, "names", names, names, sizeof(names));
    alpaca->load(keyHeader, "focusoffsets", focusoffsets, focusoffsets, sizeof(focusoffsets));
}

void CFocuser::subLoad(CAlpaca *alpaca)
{
    maxSteps= alpaca->load(keyHeader, "maxSteps", maxSteps);
    stepSize= alpaca->load(keyHeader, "stepSize", stepSize);
    if (maxSpeed!=-1) maxSpeed= alpaca->load(keyHeader, "maxSpeed", maxSpeed);
    if (msToMaxSpeed!=-1) msToMaxSpeed= alpaca->load(keyHeader, "msToMaxSpeed", msToMaxSpeed);
    if (motorDirection!=-1) motorDirection= alpaca->load(keyHeader, "motorDirection", motorDirection);
}

void CTelescope::subLoad(CAlpaca *alpaca)
{
    alpaca->load(keyHeader, "mount", (uint8_t*)&mount, sizeof(mount));
}


static std::atomic<int> transactionsId= 0; // transactio counter....

// Various http replies/headers...
static char const httpokheader[]= "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nTransfer-Encoding: chunked\r\n\r\n";
static char const http404header[]= "HTTP/1.1 404 Page Not Found\r\n\r\n";
static char const http400header[]= "HTTP/1.1 400 Bad Request\r\n\r\n";
// These are for debug purpose...
static void inline send2(SOCKET s, char *t, int l, int f) { t[l]= 0; packdebug(t); send(s, t, l, f); }
static void inline send2(SOCKET s, char const *t, int l, int f) { packdebug(t); send(s, t, l, f); }

// return the device from a url that points to Focusser/0 or something like that. moves url to the next "word" in the url for further processing (skipping '/' if needed)
CAlpacaDevice *CAlpaca::deviceFromURL(char *&url)
{
    for (int i=0; i<nbDevices; i++)
    {
        CAlpacaDevice *d= devices[i];
        char const *type= d->get_type(); 
        if (!startsWithNonCase(url, type)) continue;       // does device type match api request???
        char *t= url+strlen(type); if (*t!='/') continue; t++;// yes, skip it and /
        int id= readInt(t); if (d->id!=id) continue;       // does id match api request?
        if (*t!='/') continue;                           // yes, skip that from url (and/)
        url= t+1; return d;
    }
    return nullptr;
}

// Main alpaca http request handleing!
// sock is passed here to be able to send the reply. get is true on a http/GET and false on a http/PUT url is the url and data any extra data passed
// typically looks like this: parameter_name_1=...&parameter_name_2=...&...
bool CAlpaca::execRequest(int sock, bool get, char *url, char *data)
{
    // Find the client transaciton id if present in data! and copy it in ClientTransactionId
    char ClientTransactionId[9]= "0";
    if (data!=nullptr)
    {
        char *q= strstr(data, "ClientTransactionID=");
        if (q!=nullptr)
        {
            q+=20;
            int i=0; while (i<8 && q[i]>='0' && q[i]<='9') { ClientTransactionId[i]= q[i]; i++; }
            ClientTransactionId[i]= 0;
        }
    }
    CMyStr s; s+="        \r\n"; // space for size! see sendChunk
    s.printf("{\"ClientTransactionID\":%s,\"ServerTransactionID\":%d,", ClientTransactionId, transactionsId++); // start with known needed data!

    if (memcmp(url, "/management", 11)==0) // Management APIS
    {
        if (get && strcmp(url, "/management/apiversions")==0) s+= "\"ErrorNumber\":0,\"ErrorMessage\":\"\",\"Value\":[1]}";
        else if (get && strcmp(url, "/management/v1/description")==0) s.printf("\"Value\":{\"ServerName\":\"%s\",\"Manufacturer\":\"%s\",\"ManufacturerVersion\":\"%s\",\"Location\":\"%s\"},\"ErrorNumber\":0,\"ErrorMessage\":\"\"}", ServerName, Manufacturer, ManufacturerVersion, Location);
        else if (get && strcmp(url, "/management/v1/configureddevices")==0)
        {   // device enumeration
            s+= "\"ErrorNumber\":0,\"ErrorMessage\":\"\",\"Value\":[";
            for (int i=0; i<nbDevices; i++) s+= devices[i]->_jsonDesc;
            if (s.c[s.w-1]==',') s.w--; // remove final ',' if there is one...
            s+= "]}";
        }
    } else if (memcmp(url, "/setup", 6)==0) // Setup API
    {
        url+= 6; if (memcmp(url, "/v1/",4)!=0) return setup(sock, get, data); // System setup API
        // device setup URL
        url+= 4; 
        CAlpacaDevice *d= deviceFromURL(url);
        if (d==nullptr) { er400: send2(sock, http400header, sizeof(http400header)-1, 0); return false; } // device not known! Error
        if (strcmp(url, "setup")!=0) goto er400; // it was not setup that was called!
        return d->setup(this, sock, get, data);  // call the setup for that device!
    } else if (memcmp(url, "/api/v1/", 8)==0) // device apis...
    {
        url+= 8; CAlpacaDevice *d= deviceFromURL(url);
        if (d==nullptr) { er400_2: send2(sock, http400header, sizeof(http400header)-1, 0); return false; } // device not known! Error
        // url now points on API name/command and we have the right device! dispatch on virtual fuction
        if (!d->dispatch(get, url, data, &s)) goto er400_2; // not ok, return error....
        if (s.c[s.w-1]==',') s.w--; s+= "}"; // close object if/as needed!
    } else return setup(sock, get, data);

    // default case, send s (a json data) to the server
    send(sock, httpokheader, sizeof(httpokheader)-1, 0); // sendHTTP header...
    // s was started by 8 spaces and a crlf. Will write the chunk lengh in that space and add a crfl0 after that and then send it
    // This is the normal structure of a http response in chuck encoded format...
    size_t l= s.w-10; // This is to account for the header!
    if (l==0) { send(sock, "0\r\n", 3, 0); return true; }
    static char const hex[]="0123456789abcdef";
    for (int i=0; i<8; i++) { s.c[i]=hex[(l>>28)&0xf]; l<<=4; }
    s.append("\r\n0\r\n\r\n"); // Add terminator
    int skip= 0; while (s.c[skip]=='0') skip++; // skip the initial zeros in size
    send2(sock, s.c+skip, int(s.w-skip), 0); // and send!
    return true;
}

// error to text...
static char const *msgFromEr(TAlpacaErr er)
{
    switch (er)
    {
    case ALPACA_OK: return "";
    case ALPACA_ERR_NOT_IMPLEMENTED: return "ALPACA_ERR_NOT_IMPLEMENTED";
    case ALPACA_ERR_INVALID_VALUE: return "ALPACA_ERR_INVALID_VALUE";
    case ALPACA_ERR_VALUE_NOT_SET: return "ALPACA_ERR_VALUE_NOT_SET";
    case ALPACA_ERR_NOT_CONNECTED: return "ALPACA_ERR_NOT_CONNECTED";
    case ALPACA_ERR_INVALID_WHILE_PARKED: return "ALPACA_ERR_INVALID_WHILE_PARKED";
    case ALPACA_ERR_INVALID_WHILE_SLAVED: return "ALPACA_ERR_INVALID_WHILE_SLAVED";
    case ALPACA_ERR_INVALID_OPERATION: return "ALPACA_ERR_INVALID_OPERATION";
    case ALPACA_ERR_ACTION_NOT_IMPLEMENTED: return "ALPACA_ERR_ACTION_NOT_IMPLEMENTED";
    }
    return "ALPACA Unknown Error";
}

// A series of functions that will add standard stuff in json format (depending on type of course)
// They all return true as this is the generic all went well return for the dispatch functions...
// The only difference between them if the value type (if there is one)...
static bool putEr(CMyStr *s, TAlpacaErr er)
{
    s->printf("\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\",", er, msgFromEr(er));
    return true;
}
static bool putErVal(CMyStr *s, TAlpacaErr er, int v)
{
    s->printf("\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\",\"Value\":%d", er, msgFromEr(er), v);
    return true;
}
static bool putErVal(CMyStr *s, TAlpacaErr er, uint32_t v)
{
    s->printf("\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\",\"Value\":%d", er, msgFromEr(er), v);
    return true;
}
#ifndef _WIN32
static bool putErVal(CMyStr *s, TAlpacaErr er, int32_t v)
{
    s->printf("\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\",\"Value\":%d", er, msgFromEr(er), v);
    return true;
}
#endif
static bool putErVal(CMyStr *s, TAlpacaErr er, float v)
{
    s->printf("\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\",\"Value\":%f", er, msgFromEr(er), v);
    return true;
}
static bool putErVal(CMyStr *s, TAlpacaErr er, bool v)
{
    s->printf("\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\",\"Value\":%s", er, msgFromEr(er), v?"true":"false");
    return true;
}
static bool putErVal(CMyStr *s, TAlpacaErr er, char const * v, bool transformquotes=false)
{
    if (v==nullptr) v= "";
    s->printf("\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\",\"Value\":\"%s\"", er, msgFromEr(er), v);
    return true;
}
static bool putErValRaw(CMyStr *s, TAlpacaErr er, char const * v, bool transformquotes=false)
{
    char *c= s->c+s->w;
    s->printf("\"ErrorNumber\":%d,\"ErrorMessage\":\"%s\",\"Value\":%s", er, msgFromEr(er), v);
    if (!transformquotes) return true;
    while (*c!=0) if (*c++=='\'') c[-1]='"';
    return true;
}

// Series of functions that will look for the value for a given parameter in the input...
// They differ by the type returned...
// note that if you want to use a string stuff directly, you better verify and sanitize it (there is a function for that, see lower)
char *getStrData(char *m, char const *parameter)
{
    if (m==nullptr) return nullptr;
    size_t plen= strlen(parameter);
    while (true) // search parameter=value& in string...
    {
        if (memcmp(m, parameter, plen)==0 && m[plen]=='=') return m+plen+1;
        m= strchr(m, '&'); if (m==nullptr) return nullptr; m++;
    }
}
// return 0 or 1 (false/true) or 2 for neither if you care!
int getBoolData(char *m, char const *parameter)
{
    char const *r= getStrData(m, parameter); if (r==nullptr) return 2;
    if (memcmp(r, "True",4)==0) return 1;
    if (memcmp(r, "False",5)==0) return 0;
    if (memcmp(r, "true",4)==0) return 1;
    if (memcmp(r, "false",5)==0) return 0;
    return 2;
}
int getIntData(char *m, char const *parameter)
{
    char const *r= getStrData(m, parameter); if (r==nullptr) return -1;
    return readInt(r);
}
int getIntDataDef(char *m, char const *parameter, int def)
{
    char const *r= getStrData(m, parameter); if (r==nullptr) return def;
    if (*r<'0' || *r>'9') return def;
    return readInt(r);
}
float getFloatDataDef(char *m, char const *parameter, float def=0.0f)
{
    char const *r= getStrData(m, parameter); if (r==nullptr) return def;
    bool neg= false; if (*r=='-') neg= true, r++;
    float ret= 0;
    if (*r<'0' || *r>'9') return def;
    while (*r>='0' && *r<='9') ret= ret*10+*r++-'0';
    if (*r=='.')
    {
        r++;
        float d= 0.1f;
        while (*r>='0' && *r<='9') { ret+= (*r++-'0')*d; d/= 10.0f; }
    }
    if (!neg) return ret; return -ret;
}
bool getIntData(char *m, char const *parameter, int &v)
{
    char const *r= getStrData(m, parameter); if (r==nullptr) return false;
    if (*r<'0' || *r>'9') return false;
    v= readInt(r);
    return true;
}
bool getFloatData(char *m, char const *parameter, float &v)
{
    char const *r= getStrData(m, parameter); if (r==nullptr) return false;
    if ((*r<'0' || *r>'9') && *r!='-') return false;
    v= readFloat(r);
    return true;
}

// This is called in response to a http request and handles things which are common to all devices...
bool CAlpacaDevice::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    if (!get && strcmp(url, "action") == 0) return putEr(s, action(getStrData(data, "Action"), getStrData(data,"Parameters")));
    if (!get && strcmp(url, "commandblind") == 0) return putEr(s, commandblind(getStrData(data, "Command")));
    if (!get && strcmp(url, "commandbool") == 0)
    {
        bool resp= true;
        return putErVal(s, commandbool(getStrData(data, "Command"), &resp), resp);
    }
    if (!get && strcmp(url, "commandstring") == 0)
    {
        char res[256]= "";
        return putErVal(s, commandstring(getStrData(data, "Command"), res, sizeof(res)), res);
    }
    if (!get && strcmp(url, "connect") == 0) return putEr(s, set_connected(true));
    if (!get && strcmp(url, "disconnect") == 0) return putEr(s, set_connected(false));
    if (get && strcmp(url, "connected") == 0) return putErVal(s, ALPACA_OK, get_connected());
    if (!get && strcmp(url, "connected") == 0) return putEr(s, set_connected(getBoolData(data,"Connected")));
    if (get && strcmp(url, "connecting") == 0) return putErVal(s, ALPACA_OK, get_connecting());
    if (get && strcmp(url, "driverinfo") == 0) return putErVal(s, ALPACA_OK, get_driverinfo());
    if (get && strcmp(url, "description") == 0) return putErVal(s, ALPACA_OK, get_description());
    if (get && strcmp(url, "driverversion") == 0) return putErVal(s, ALPACA_OK, get_driverversion());
    if (get && strcmp(url, "name") == 0) return putErVal(s, ALPACA_OK, get_name());
    if (get && strcmp(url, "interfaceversion") == 0) return putErVal(s, ALPACA_OK, get_interfaceversion());
    if (get && strcmp(url, "supportedactions") == 0) return putErValRaw(s, ALPACA_OK, get_supportedactions());
    return putEr(s, ALPACA_ERR_INVALID_OPERATION);
}

// These is the http handeling for Telescope.The basic dispatch for all commands
bool CTelescope::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    if (get && strcmp(url, "alignmentmode")==0) return putErVal(s, ALPACA_OK, alignmentmode());
    if (get && strcmp(url, "equatorialsystem")==0) return putErVal(s, ALPACA_OK, equatorialsystem());
    if (get && strcmp(url, "aperturearea")==0) return putErVal(s, ALPACA_OK, get_aperturearea());
    if (!get && strcmp(url, "aperturearea")==0) return putEr(s, set_aperturearea(getFloatDataDef(data, "ApertureArea", 0.0f)));
    if (get && strcmp(url, "aperturediameter")==0) return putErVal(s, ALPACA_OK, get_aperturediameter());
    if (!get && strcmp(url, "aperturediameter")==0) return putEr(s, set_aperturediameter(getFloatDataDef(data, "ApertureDiameter", 0.0f)));
    if (get && strcmp(url, "focallength")==0) return putErVal(s, ALPACA_OK, get_focallength());
    if (!get && strcmp(url, "focallength")==0) return putEr(s, set_focallength(getFloatDataDef(data, "FocalLength", 0.0f)));
    if (get && strcmp(url, "siteelevation")==0) return putErVal(s, ALPACA_OK, get_siteelevation());
    if (!get && strcmp(url, "siteelevation")==0) return putEr(s, set_siteelevation(getFloatDataDef(data, "SiteElevation", 0.0f)));
    if (get && strcmp(url, "sitelatitude")==0) return putErVal(s, ALPACA_OK, get_sitelatitude());
    if (!get && strcmp(url, "sitelatitude")==0) return putEr(s, set_sitelatitude(getFloatDataDef(data, "SiteLatitude", 0.0f)));
    if (get && strcmp(url, "sitelongitude")==0) return putErVal(s, ALPACA_OK, get_sitelongitude());
    if (!get && strcmp(url, "sitelongitude")==0) return putEr(s, set_sitelongitude(getFloatDataDef(data, "SiteLongitude", 0.0f)));
    
    if (get && strcmp(url, "canfindhome")==0) return putErVal(s, ALPACA_OK, canfindhome());
    if (get && strcmp(url, "athome")==0) return putErVal(s, ALPACA_OK, athome());
    if (!get && strcmp(url, "findhome")==0) return putEr(s, findhome());
    if (get && strcmp(url, "canpark")==0) return putErVal(s, ALPACA_OK, canpark());
    if (get && strcmp(url, "atpark")==0) return putErVal(s, ALPACA_OK, atpark());
    if (get && strcmp(url, "cansetpark")==0) return putErVal(s, ALPACA_OK, cansetpark());
    if (get && strcmp(url, "canunpark")==0) return putErVal(s, ALPACA_OK, canunpark());
    if (!get && strcmp(url, "park")==0) return putEr(s, park());
    if (!get && strcmp(url, "setpark")==0) return putEr(s, setpark());
    if (!get && strcmp(url, "unpark")==0) return putEr(s, unpark());

    if (get && strcmp(url, "canpulseguide")==0) return putErVal(s, ALPACA_OK, canpulseguide());
    if (!get && strcmp(url, "pulseguide")==0) return putEr(s, pulseguide(getIntData(data,"Direction"), getIntData(data,"Duration")));
    if (get && strcmp(url, "ispulseguiding")==0) return putErVal(s, ALPACA_OK, ispulseguiding());
    if (get && strcmp(url, "cansetguiderates")==0) return putErVal(s, ALPACA_OK, cansetguiderates());
    if (get && strcmp(url, "guideratedeclination")==0) return putErVal(s, ALPACA_OK, get_guideratedeclination());
    if (!get && strcmp(url, "guideratedeclination")==0) return putEr(s, set_guideratedeclination(getFloatDataDef(data, "GuideRateDeclination", 0.0f)));
    if (get && strcmp(url, "guideraterightascension")==0) return putErVal(s, ALPACA_OK, get_guideraterightascension());
    if (!get && strcmp(url, "guideraterightascension")==0) return putEr(s, set_guideraterightascension(getFloatDataDef(data, "GuideRateRightAscension", 0.0f)));

    if (get && strcmp(url, "cansettracking")==0) return putErVal(s, ALPACA_OK, cansettracking());
    if (get && strcmp(url, "tracking")==0) return putErVal(s, ALPACA_OK, get_tracking());
    if (!get && strcmp(url, "tracking")==0) return putEr(s, set_tracking(getBoolData(data, "Tracking")));
    if (get && strcmp(url, "trackingrate")==0) return putErVal(s, ALPACA_OK, get_trackingrate());
    if (get && strcmp(url, "trackingrates")==0) return putErValRaw(s, ALPACA_OK, trackingrates());
    if (!get && strcmp(url, "trackingrate")==0) return putEr(s, set_trackingrate(getIntData(data, "TrackingRate")));

    if (get && strcmp(url, "cansetrightascensionrate")==0) return putErVal(s, ALPACA_OK, cansetrightascensionrate());
    if (get && strcmp(url, "rightascensionrate")==0) return putErVal(s, ALPACA_OK, get_rightascensionrate());
    if (!get && strcmp(url, "rightascensionrate")==0) return putEr(s, set_rightascensionrate(getFloatDataDef(data, "RightAscensionRate", 0.0f)));
    if (get && strcmp(url, "cansetdeclinationrate")==0) return putErVal(s, ALPACA_OK, cansetdeclinationrate());
    if (get && strcmp(url, "declinationrate")==0) return putErVal(s, ALPACA_OK, get_declinationrate());
    if (!get && strcmp(url, "declinationrate")==0) return putEr(s, set_declinationrate(getFloatDataDef(data, "DeclinationRate", 0.0f)));

    if (get && strcmp(url, "slewing")==0) return putErVal(s, ALPACA_OK, slewing());
    if (get && strcmp(url, "slewsettletime")==0) return putErVal(s, ALPACA_OK, get_slewsettletime());
    if (!get && strcmp(url, "slewsettletime")==0) return putEr(s, set_slewsettletime(getIntData(data, "SlewSettleTime")));
    if (!get && strcmp(url, "abortslew")==0) return putEr(s, abortslew());
    if (get && strcmp(url, "canslew")==0) return putErVal(s, ALPACA_OK, canslew());
    if (get && strcmp(url, "canslewasync")==0) return putErVal(s, ALPACA_OK, canslewasync());
    if (!get && strcmp(url, "slewtocoordinates")==0) return putEr(s, slewtocoordinates(getFloatDataDef(data, "RightAscension", 0.0f), getFloatDataDef(data, "Declination", 0.0f)));
    if (!get && strcmp(url, "slewtocoordinatesasync")==0) return putEr(s, slewtocoordinatesasync(getFloatDataDef(data, "RightAscension", 0.0f), getFloatDataDef(data, "Declination", 0.0f)));
    if (get && strcmp(url, "cansync")==0) return putErVal(s, ALPACA_OK, cansync());
    if (!get && strcmp(url, "synctocoordinates")==0) return putEr(s, synctocoordinates(getFloatDataDef(data, "RightAscension", 0.0f), getFloatDataDef(data, "Declination", 0.0f)));

    if (get && strcmp(url, "targetdeclination")==0) return putErVal(s, ALPACA_OK, get_targetdeclination());
    if (!get && strcmp(url, "targetdeclination")==0) return putEr(s, set_targetdeclination(getFloatDataDef(data, "TargetDeclination", 0.0f)));
    if (get && strcmp(url, "targetrightascension")==0) return putErVal(s, ALPACA_OK, get_targetrightascension());
    if (!get && strcmp(url, "targetrightascension")==0) return putEr(s, set_targetrightascension(getFloatDataDef(data, "TargetRightAscension", 0.0f)));
    if (get && strcmp(url, "slewtotarget")==0) return putErVal(s, ALPACA_OK, slewtotarget());
    if (get && strcmp(url, "slewtotargetasync")==0) return putErVal(s, ALPACA_OK, slewtotargetasync());
    if (!get && strcmp(url, "synctotarget")==0) return putErVal(s, ALPACA_OK, synctotarget());

    if (get && strcmp(url, "declination")==0) return putErVal(s, ALPACA_OK, declination());
    if (get && strcmp(url, "rightascension")==0) return putErVal(s, ALPACA_OK, rightascension());

    if (get && strcmp(url, "canslewaltaz")==0) return putErVal(s, ALPACA_OK, canslewaltaz());
    if (get && strcmp(url, "canslewaltazasync")==0) return putErVal(s, ALPACA_OK, canslewaltazasync());
    if (get && strcmp(url, "cansyncaltaz")==0) return putErVal(s, ALPACA_OK, cansyncaltaz());
    if (!get && strcmp(url, "slewtoaltaz")==0) return putEr(s, slewtoaltaz(getFloatDataDef(data, "Azimuth", 0.0f), getFloatDataDef(data, "Altitude", 0.0f)));
    if (!get && strcmp(url, "slewtoaltazasync")==0) return putEr(s, slewtoaltazasync(getFloatDataDef(data, "Azimuth", 0.0f), getFloatDataDef(data, "Altitude", 0.0f)));
    if (!get && strcmp(url, "synctoaltaz")==0) return putEr(s, synctoaltaz(getFloatDataDef(data, "Azimuth", 0.0f), getFloatDataDef(data, "Altitude", 0.0f)));
    if (get && strcmp(url, "altitude")==0) { float v=0.0f; return putErVal(s, altitude(v), v); }
    if (get && strcmp(url, "azimuth")==0) { float v=0.0f; return putErVal(s, azimuth(v), v); }

    if (get && strcmp(url, "doesrefraction")==0) return putErVal(s, ALPACA_OK, get_doesrefraction());
    if (!get && strcmp(url, "doesrefraction")==0) return putEr(s, set_doesrefraction(getBoolData(data, "DoesRefraction")));

    if (get && strcmp(url, "cansetpierside")==0) return putErVal(s, ALPACA_OK, cansetpierside());
    if (get && strcmp(url, "sideofpier")==0) return putErVal(s, ALPACA_OK, get_sideofpier());
    if (!get && strcmp(url, "sideofpier")==0) return putEr(s, set_sideofpier(getIntData(data, "SideOfPier")));
    if (get && strcmp(url, "destinationsideofpier")==0) return putErVal(s, ALPACA_OK, destinationsideofpier(getFloatDataDef(data, "RightAscension", 0.0f), getFloatDataDef(data, "Declination", 0.0f)));

    if (get && strcmp(url, "siderealtime")==0) { float v=0.0f; return putErVal(s, siderealtime(v), v); }
    if (get && strcmp(url, "utcdate")==0) { char b[30]; return putErVal(s, get_utcdate(b), b); }
    if (!get && strcmp(url, "utcdate")==0) return putEr(s, set_utcdate(getStrData(data, "UTCDate")));

    if (get && strcmp(url, "axisrates")==0) { char b[50]; return putErValRaw(s, axisrates(getIntData(data, "Axis"), b), b); }
    if (get && strcmp(url, "canmoveaxis")==0) return putErVal(s, ALPACA_OK, canmoveaxis(getIntData(data, "Axis")));
    if (!get && strcmp(url, "moveaxis")==0) return putEr(s, moveaxis(getIntData(data, "Axis"), getFloatDataDef(data, "Rate", 0.0f)));

    return CAlpacaDevice::dispatch(get, url, data, s);
}
#ifdef HASMilisecondTime  // define this if you have a function called Milisecond which will return a time counter in milisecond. This is used for UTCTime in CTelescope.
uint32_t Milisecond(); // Must be defined elsewhere! else do not #def HASMilisecondTime
static uint64_t UTCTimeDelta= 0;
// format is: 2025-12-02T16:13:09.0146526Z
static int8_t const dpm[]={31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
TAlpacaErr CTelescope::set_utcdate(char const* B) // Here we save the time given by the PC (accurate to 1s for the moment) and make a delta with our ms clock...
{
    char b2[100]; getHtmlString(B, b2, int(sizeof(b2))); char const *b= b2;
    int Y, M, D, h, m, s;
    if (!readInt(b, Y)) return ALPACA_ERR_INVALID_VALUE; if (*b++!='-') return ALPACA_ERR_INVALID_VALUE; if (Y<2024 || Y>2099) return ALPACA_ERR_INVALID_VALUE; Y-=2024;
    if (!readInt(b, M)) return ALPACA_ERR_INVALID_VALUE; if (*b++!='-') return ALPACA_ERR_INVALID_VALUE; if (M<1 || M>12) return ALPACA_ERR_INVALID_VALUE;
    if (!readInt(b, D)) return ALPACA_ERR_INVALID_VALUE; if (*b++!='T') return ALPACA_ERR_INVALID_VALUE; if (D<1 || D>31) return ALPACA_ERR_INVALID_VALUE;
    if (!readInt(b, h)) return ALPACA_ERR_INVALID_VALUE; if (*b++!=':') return ALPACA_ERR_INVALID_VALUE; if (h<0 || h>23) return ALPACA_ERR_INVALID_VALUE;
    if (!readInt(b, m)) return ALPACA_ERR_INVALID_VALUE; if (*b++!=':') return ALPACA_ERR_INVALID_VALUE; if (m<0 || m>59) return ALPACA_ERR_INVALID_VALUE;
    if (!readInt(b, s)) return ALPACA_ERR_INVALID_VALUE; if (*b++!='.') return ALPACA_ERR_INVALID_VALUE; if (s<0 || s>59) return ALPACA_ERR_INVALID_VALUE;
    h= h*3600+m*60+s;
    while (Y>=4) D+= 365*4+1, Y-=4; if (M>2 && Y==0) D++; while (Y>0) D+= 365, Y--;
    for (int i=0; i<M-1; i++) D+= dpm[i];
    UTCTimeDelta= uint64_t(D)*24*3600+h-Milisecond()/1000;
    return ALPACA_OK;
}
TAlpacaErr CTelescope::get_utcdate(char* b) // here we add our ms clock to the delta given by the PC and generate UTC time...
{
    if (UTCTimeDelta==0) { b[0]= 0; return ALPACA_ERR_VALUE_NOT_SET; } // not yet set, return an error...
    uint64_t now= UTCTimeDelta*1000+Milisecond();
    int Y, M, D, h, m, s, ms;
    h= now%(24*3600*1000);
    D= int(now/(24*3600*1000));
    ms= h%1000; h/= 1000;
    s= h%60; h/=60;
    m= h%60; h/=60;
    Y= 2024;
    while (D>=((Y%3)==0?366:365)) D-=(Y%3)==0?366:365, Y++;
    M= 1;
    while (D>=dpm[M-1]+((M==2 && (Y%3)==0)?1:0))
        D-= dpm[M-1]+((M==2 && (Y%3)==0)?1:0), M++;
    D++;
    sprintf(b, "%04d-%02d-%02dT%02d:%02d:%02d.%04d000Z", Y, M, D, h, m, s, ms);
    return ALPACA_OK;
}
#endif

// These is the http handeling for Focuser.The basic dispatch for all commands
bool CFocuser::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    if (get && strcmp(url, "absolute") == 0) return putErVal(s, ALPACA_OK, get_absolute());
    if (get && strcmp(url, "maxincrement") == 0) return putErVal(s, ALPACA_OK, get_maxincrement());
    if (get && strcmp(url, "maxstep") == 0) return putErVal(s, ALPACA_OK, get_maxstep());
    if (get && strcmp(url, "position") == 0) return putErVal(s, ALPACA_OK, get_position());
    if (get && strcmp(url, "stepsize") == 0) return putErVal(s, ALPACA_OK, get_stepsize());
    if (get && strcmp(url, "ismoving") == 0) return putErVal(s, ALPACA_OK, get_ismoving());
    if (get && strcmp(url, "tempcomp") == 0) { bool comp= false; return putErVal(s, get_tempcomp(&comp), comp); }
    if (get && strcmp(url, "tempcompavailable") == 0) return putErVal(s, ALPACA_OK, get_tempcompavailable());
    if (get && strcmp(url, "temperature") == 0) { float temp= 0.0f; return putErVal(s, get_temperature(&temp), temp); }

    if (!get && strcmp(url, "tempcomp") == 0) return putEr(s, put_tempcomp(getBoolData(data, "TempComp")));
    if (!get && strcmp(url, "halt") == 0) return putEr(s, put_halt());
    if (!get && strcmp(url, "move") == 0) return putEr(s, put_move(getIntData(data, "Position")));
    return CAlpacaDevice::dispatch(get, url, data, s);
}

// These is the http handeling for FilterWheel. The basic dispatch for all commands
bool CFilterWheel::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    if (get && strcmp(url, "focusoffsets") == 0) return putErValRaw(s, ALPACA_OK, get_focusoffsets());
    if (get && strcmp(url, "names") == 0) return putErValRaw(s, ALPACA_OK, get_names(), true);

    if (get && strcmp(url, "position") == 0) return putErVal(s, ALPACA_OK, get_position());
    if (!get && strcmp(url, "position") == 0) return putEr(s, put_position(getIntData(data, "Position")));
    if (!get && strcmp(url, "move") == 0) return putEr(s, put_position(getIntData(data, "Position")));
    return CAlpacaDevice::dispatch(get, url, data, s);
}

// These is the http handeling for rotator.The basic dispatch for all commands
bool CRotator::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    bool b=false; float v=0.0;
    if (get && strcmp(url, "canreverse") == 0) return putErVal(s, get_canreverse(&b), b);
    if (get && strcmp(url, "reverse") == 0) return putErVal(s, get_reverse(&b), b);
    if (!get && strcmp(url, "reverse") == 0) return putEr(s, put_reverse(getBoolData(data, "Reverse")));
    if (get && strcmp(url, "stepsize") == 0) return putErVal(s, get_stepsize(&v), v);
    if (get && strcmp(url, "ismoving") == 0) return putErVal(s, get_ismoving(&b), b);
    if (get && strcmp(url, "position") == 0) return putErVal(s, get_position(&v), v);
    if (get && strcmp(url, "mechanicalposition") == 0) return putErVal(s, get_mechanicalposition(&v), v);
    if (!get && strcmp(url, "sync") == 0) return putEr(s, put_sync(getFloatDataDef(data, "Position", 0.0f)));
    if (get && strcmp(url, "targetposition") == 0) return putErVal(s, get_targetposition(&v), v);
    if (!get && strcmp(url, "move") == 0) return putEr(s, put_move(getFloatDataDef(data, "Position")));
    if (!get && strcmp(url, "moveabsolute") == 0) return putEr(s, put_moveabsolute(getFloatDataDef(data, "Position")));
    if (!get && strcmp(url, "movemechanical") == 0) return putEr(s, put_movemechanical(getFloatDataDef(data, "Position")));
    return CAlpacaDevice::dispatch(get, url, data, s);
}


// These is the http handeling for switches.The basic dispatch for all commands
bool CSwitch::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    int32_t id=getIntData(data, "Id"), vi=0; bool vb=false; float vd=0.0;
    char b[256];
    if (get && strcmp(url, "maxswitch") == 0) return putErVal(s, get_maxswitch(&vi), vi);

    if (get && strcmp(url, "canwrite") == 0) return putErVal(s, get_canwrite(id, &vb), vb);
    if (get && strcmp(url, "getswitch") == 0) return putErVal(s, get_getswitch(id, &vb), vb);
    if (!get && strcmp(url, "setswitch") == 0) return putEr(s, put_setswitch(id, getBoolData(data, "State")));

    if (get && strcmp(url, "getswitchname") == 0) return putErVal(s, get_getswitchname(id, b, sizeof(b)), b);
    if (!get && strcmp(url, "putswitchname") == 0) return putEr(s, put_setswitchname(id, getStrData(data, "Name")));
    if (get && strcmp(url, "getswitchdescription") == 0) return putErVal(s, get_getswitchdescription(id, b, sizeof(b)), b);
    if (!get && strcmp(url, "putswitchdescription") == 0) return putEr(s, put_setswitchdescription(id, getStrData(data, "Description")));

    if (get && strcmp(url, "switchstep") == 0) return putErVal(s, get_switchstep(id, &vd), vd);
    if (get && strcmp(url, "getswitchvalue") == 0) return putErVal(s, get_getswitchvalue(id, &vd), vd);
    if (!get && strcmp(url, "setswitchvalue") == 0) return putEr(s, put_setswitchvalue(id, getFloatDataDef(data, "Value")));
    if (get && strcmp(url, "minswitchvalue") == 0) return putErVal(s, get_minswitchvalue(id, &vd), vd);
    if (get && strcmp(url, "maxswitchvalue") == 0) return putErVal(s, get_maxswitchvalue(id, &vd), vd);

    
    if (get && strcmp(url, "canasync") == 0) return putErVal(s, get_canasync(id, &vb), vb);
    if (!get && strcmp(url, "setasync") == 0) return putEr(s, put_setasync(id, getBoolData(data, "State")));
    if (!get && strcmp(url, "setasyncvalue") == 0) return putEr(s, put_setasyncvalue(id, getFloatDataDef(data, "Value")));
    if (get && strcmp(url, "statechangecomplete") == 0) return putErVal(s, get_statechangecomplete(id, &vb), vb);

    return CAlpacaDevice::dispatch(get, url, data, s);
}

// These is the http handeling for switches.The basic dispatch for all commands
bool CSafetyMonitor::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    bool v= false;
    if (get && strcmp(url, "issafe") == 0) return putErVal(s, get_issafe(&v), v);
    return CAlpacaDevice::dispatch(get, url, data, s);
}

// These is the http handeling for CoverCalibrator.The basic dispatch for all commands
bool CCoverCalibrator::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    uint32_t vi=0; bool vb=false; TAlpacaErr er;
    if (get && strcmp(url, "brightness") == 0) return putErVal(s, get_brightness(&vi), vi);
    if (get && strcmp(url, "calibratorchanging") == 0) return putErVal(s, get_calibratorchanging(&vb), vb);
    if (get && strcmp(url, "calibratorstate") == 0) { CalibratorState v; er= get_calibratorstate(&v); return putErVal(s, er, int(v)); }
    if (get && strcmp(url, "covermoving") == 0) return putErVal(s, get_covermoving(&vb), vb);
    if (get && strcmp(url, "coverstate") == 0) { CoverState v; er= get_coverstate(&v); return putErVal(s, er, int(v)); }
    if (get && strcmp(url, "maxbrightness") == 0) return putErVal(s, get_maxbrightness(&vi), vi);

    if (!get && strcmp(url, "calibratoroff") == 0) return putEr(s, calibratoroff());
    if (!get && strcmp(url, "calibratoron") == 0) return putEr(s, calibratoron(getIntData(data, "Brightness")));
    if (!get && strcmp(url, "calibratoroff") == 0) return putEr(s, calibratoroff());

    if (get && strcmp(url, "closecover") == 0) return putEr(s, closecover());
    if (get && strcmp(url, "opencover") == 0) return putEr(s, opencover());
    if (get && strcmp(url, "haltcover") == 0) return putEr(s, haltcover());

    return CAlpacaDevice::dispatch(get, url, data, s);
}

// These is the http handeling for ObservingConditions.The basic dispatch for all commands
bool CObservingConditions::dispatch(bool get, char const *url, char *data, CMyStr *s)
{
    float v= 0.0; 
    if (get && strcmp(url, "averageperiod") == 0) return putErVal(s, get_averageperiod(&v), v);
    if (!get && strcmp(url, "averageperiod") == 0) return putEr(s, put_averageperiod(getFloatDataDef(data, "AveragePeriod")));

    if (get && strcmp(url, "cloudcover") == 0) return putErVal(s, get_cloudcover(&v), v);
    if (get && strcmp(url, "dewpoint") == 0) return putErVal(s, get_dewpoint(&v), v);
    if (get && strcmp(url, "humidity") == 0) return putErVal(s, get_humidity(&v), v);
    if (get && strcmp(url, "pressure") == 0) return putErVal(s, get_pressure(&v), v);
    if (get && strcmp(url, "rainrate") == 0) return putErVal(s, get_rainrate(&v), v);
    if (get && strcmp(url, "skybrightness") == 0) return putErVal(s, get_skybrightness(&v), v);
    if (get && strcmp(url, "skyquality") == 0) return putErVal(s, get_skyquality(&v), v);
    if (get && strcmp(url, "skytemperature") == 0) return putErVal(s, get_skytemperature(&v), v);
    if (get && strcmp(url, "starfwhm") == 0) return putErVal(s, get_starfwhm(&v), v);
    if (get && strcmp(url, "temperature") == 0) return putErVal(s, get_temperature(&v), v);
    if (get && strcmp(url, "winddirection") == 0) return putErVal(s, get_winddirection(&v), v);
    if (get && strcmp(url, "windgust") == 0) return putErVal(s, get_windgust(&v), v);
    if (get && strcmp(url, "windspeed") == 0) return putErVal(s, get_windspeed(&v), v);
    if (get && strcmp(url, "timesincelastupdate") == 0) return putErVal(s, get_timesincelastupdate(&v), v);
    if (!get && strcmp(url, "refresh") == 0) return putEr(s, put_refresh());
    if (get && strcmp(url, "sensordescription") == 0)  { char *d= getStrData(data, "SensorName"); char const *b=nullptr; return putErVal(s, get_sensordescription(d, b), b); }

    return CAlpacaDevice::dispatch(get, url, data, s);
}

// Here you will need to add dispatches for all the other classes if you want to work on them!!!
// If you need help, contact me: cyrille.de.brebisson@gmail.com

/////////////////////////////////
// html data sanitation...
// Gets a pointer to date in an html data packet (typically, just after the '=')
// And will decode and place the data in buf, limiting itself to buflen...
// return buf for convinience...
// Here are example of input...
// wifi=hpmad2&wifip=aaa&fname=FocusServer&location=Mars
// wifi=aa&wifip=a%3Db&fname=c%26d&location=e%22%22
    static int getHex(char c)
    {
        if (c>='0' && c<='9') return c-'0';
        if (c>='A' && c<='F') return c-'A'+10;
        if (c>='a' && c<='f') return c-'a'+10;
        return -1;
    }
char *getHtmlString(char const *in, char *buf, size_t buflen)
{
    int i=0;
    while (i<buflen)
    {
        if (*in<=' ' || *in=='&' || *in=='=') break;
        if (*in=='+') { buf[i++]= ' '; in++; continue; }
        if (*in!='%') { buf[i++]= *in++; continue; }
        in++;
        int vh= getHex(*in++);
        int vl= getHex(*in++);
        if (vh<0 || vl<0) break;
        buf[i++]= (vh<<4)+vl;
    }
    buf[i]= 0;
    return buf;
}

///////////////////////////////////////////////
// Setup system html/http handeling
static const char httpHtmlHeader[]="HTTP/1.0 200 OK\r\nContent-type: text/html; charset=UTF-8\r\nContent-Length: ";
static const char basicSetupPage[]=
"<html>"
"	<head>"
"		<meta name=\"viewport\" http-equiv=\"content-type\" content=\"text/html; charset=UTF-8,width=device-width, initial-scale=1.0\">"
"		<title>Alpaca server setup</title>"
"	</head>"
"	<body>"
"		<h1>Alpaca devices on this system</h1>";

static const char basicSetupPage2[]=
"		<h1>Setup for this Alpaca system</h1>"
"		<form action=\"/setup\">"
"			<table align=\"center\">"
"				<tr><td align=\"right\"><label for=\"fname\">System name:</label></td>"
"					<td><input type=\"text\" id=\"fname\" name=\"fname\" value=\"%s\"></td></tr>"
"				<tr><td align=\"right\"><label for=\"wifi\">WiFi network name:</label></td>"
"					<td><input type=\"text\" id=\"wifi\" name=\"wifi\" value=\"%s\"></td></tr>"
"				<tr><td align=\"right\"><label for=\"wifip\">WiFi password:</label></td>"
"					<td><input type=\"text\" id=\"wifip\" name=\"wifip\"></td></tr>"
"				<tr><td align=\"right\"><label for=\"location\">Location:</label></td>"
"					<td><input type=\"text\" id=\"location\" name=\"location\" value=\"%s\"></td></tr>"
"			</table><br><input type=\"submit\" value=\"Submit\">"
"		</form>";

static const char closeBodyStyleCloseHtml[]="</body>"
        "<style>"
        "	body{ background-color: black; color:white; text-align: center; }"
        "	h1{ margin-bottom: 40px; margin-top: 50px; text-align: center; }"
        "	a{ color: lightgreen; }"
        "</style>"
        "</html>";
bool CAlpaca::setup(int sock, bool get, char *data)
{
    if (data!=nullptr) // here we handle cases where we are given parameters which correspond to things that needs to be changed...
    {   // wifi=hpmad2&wifip=aaa&fname=FocusServer&location=Mars
        char const *d;
        saveLoadBegin();
        d= getStrData(data, "wifi"); if (d!=nullptr) save("wifi", getHtmlString(d, wifi, sizeof(wifi)));
        d= getStrData(data, "wifip"); if (d!=nullptr) save("wifip", getHtmlString(d, wifip, sizeof(wifip)));
        d= getStrData(data, "fname"); if (d!=nullptr) save("ServerName", getHtmlString(d, ServerName, sizeof(ServerName)));
        d= getStrData(data, "location"); if (d!=nullptr) save("Location", getHtmlString(d, Location, sizeof(Location)));
        saveLoadEnd();
    }
    CMyStr s;
    s.printf(basicSetupPage);
    for (int i=0; i<nbDevices; i++)
        s.printf("<h2><a href=\"setup/v1/%s/%d/setup\">%s %d:%s %s</a></h2>", devices[i]->get_type(), devices[i]->id, devices[i]->get_type(), devices[i]->id, devices[i]->get_name(), devices[i]->get_description());
    s+="</form>"; s+= closeBodyStyleCloseHtml;
    s.printf(basicSetupPage2, ServerName, wifi, Location);
    char t[sizeof(httpHtmlHeader)+10]; strcpy(t, httpHtmlHeader);
    char *e= t+strlen(t); _itoa(int(s.w), e, 10); strcat(e,"\r\n\r\n");
    send(sock, t, int(strlen(t)), 0);
    send(sock, s.c, int(s.w), 0);
    return true;
}

// This is the html/http handeling of device setup pages...
static const char basicDeviceSetupPage[]=
"<html><head><meta name=\"viewport\" http-equiv=\"content-type\" content=\"text/html; charset=UTF-8,width=device-width, initial-scale=1.0\"><title>%s setup</title>"
"<style>"
"/* Conteneur principal */"
".tabs-container { display: flex; flex-wrap: wrap; max-width: 600px; margin: 0 auto; }"
""
"/* On cache les boutons radio */"
".tab-logic { display: none; }"
""
".tab-label { "
"padding: 10px 20px; background: black; color: green; cursor: pointer; "
"border: 1px solid #ccc; margin-right: 2px; order: 1; /* Force l'alignement en haut */"
"}"
""
"/* Le style du contenu */"
".tab-content { "
"width: 100%%; display: none; order: 2; /* Force l'affichage sous les labels */"
"padding: 20px; border: 1px solid #ccc; background: black;"
"}"
""
".tab-logic:checked + .tab-label { background: lightgray; font-weight: bold; border-bottom: none; }"
".tab-logic:checked + .tab-label + .tab-content { display: block; }"
""
"table { width: 100%%; margin: 20px auto; border-collapse: collapse; }"
"td, th { border: 1px solid #ddd; padding: 8px; }"
"</style>"
"</head><body>"
"<h2 align=\"left\"><a href=\"/setup\">Main setup</a></h1>"
"<h1>%s %d:%s (%s)</h1>"
"<div class=\"tabs-container\">"
"<input type=\"radio\" name=\"mon_groupe\" id=\"t1\" class=\"tab-logic\" checked>"
"<label for=\"t1\" class=\"tab-label\">Alpaca Setup</label>"
"<div class=\"tab-content\">"
"<form action=\"/setup/v1/%s/%d/setup\">"
"<table align=\"center\">"
"  <tr><td align=\"right\"><label for=\"name\">name:</label></td>"
"      <td><input type=\"text\" id=\"name\" name=\"name\" value=\"%s\"></td></tr>"
"  <tr><td align=\"right\"><label for=\"description\">description:</label></td>"
"      <td><input type=\"text\" id=\"description\" name=\"description\" value=\"%s\"></td></tr></table>"
"  <input type=\"submit\" value=\"Submit\">"
"</form>"
"</div>";

bool CAlpacaDevice::setup(CAlpaca *Alpaca, int sock, bool get, char *data)
{
    if (data!=nullptr)
    {   
        char const *d; char t[30]; 
        strcpy(t, keyHeader); strcat(t, "Name");        d= getStrData(data, "name"); if (d!=nullptr) Alpaca->save(t, getHtmlString(d, Name, sizeof(Name)));
        strcpy(t, keyHeader); strcat(t, "Description"); d= getStrData(data, "description"); if (d!=nullptr) Alpaca->save(t, getHtmlString(d, Description, sizeof(Description)));
    }
    CMyStr s;
    s.printf(basicDeviceSetupPage, get_type(), get_type(), id, get_name(), get_description(), get_type(), id, get_name(), get_description());
    subSetup(Alpaca, sock, get, data, s);
    s+= closeBodyStyleCloseHtml;
    char t[sizeof(httpHtmlHeader)+10]; strcpy(t, httpHtmlHeader);
    char *e= t+strlen(t); _itoa(int(s.w), e, 10); strcat(e,"\r\n\r\n");
    send(sock, t, int(strlen(t)), 0);
    send(sock, s.c, int(s.w), 0);
    return true;
}

static size_t erasePos(char *savedPosString, char const *inname, char *name) // d is a pointer on the non sanitized name given by the user. name will contain the sanitized name (20 chr max with 0) returns the length of the string!
{
    int i; for (i=0; i<19; i++) if (*inname<'#' || *inname>'z') break; else name[i]= *inname++; name[i]= 0; // extract name
    size_t l= strlen(savedPosString); if (l>0 && savedPosString[l-1]!='!') { savedPosString[l-1]= '!'; l++; } // make sure ends with !
    // find existing name in the list...
    char *d= savedPosString;
    while (true)
    {
        int l1= 0; while (d[l1]!=0 && d[l1]!='!') l1++; if (d[l1]==0) return l; // find end of token, or end of string...
        if (memcmp(d, name, i)!=0 || d[i]!=' ') { d+= l1+1; continue; } // nope, not the right one, next one...
        strcpy(d, d+l1+1);             // erase current item...
        return strlen(savedPosString); // and return...
    }
}

void CFocuser::eraseSavedPos(CAlpaca *Alpaca, char const *name)
{
    char *savedPosString= getSavedPos(Alpaca);
    char t[21]; erasePos(savedPosString, name, t); // erase the existing version of this (if exists)
    Alpaca->save(keyHeader, "savedPos", savedPosString); // and save
}
void CFocuser::setSavedPos(CAlpaca *Alpaca, char const *name)
{
    char *savedPosString= getSavedPos(Alpaca);
    char t[21]; size_t l= erasePos(savedPosString, name, t); // erase the existing version of this (if exists)
    sprintf(savedPosString+l, "%s %d!", name, int(get_position())); // add current post at end of string
    Alpaca->save(keyHeader, "savedPos", savedPosString); // and save
}

void CFocuser::subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s)  // This allows you to add stuff in the HTML or handle inputs...
{
    if (data!=nullptr)
    {   
        // handle inboud move
        char const *d= getStrData(data, "position");
        if (d!=nullptr && *d>='0' && *d<='9') put_move(readInt(d));
        d= getStrData(data, "Distance");
        if (d!=nullptr && *d>='0' && *d<='9') 
        {
            float f= readFloat(d);
            f=f*1000.0f/get_stepsize();
            put_move(int(f));
        }

        // handle save /erase position...
        d= getStrData(data, "posname"); if (d!=nullptr) setSavedPos(Alpaca, d);
        d= getStrData(data, "erasePos"); if (d!=nullptr) eraseSavedPos(Alpaca, d);

        float vf;
        if (getFloatData(data, "stepSize", vf)) set_stepSize(Alpaca, vf);
        int vi;
        if (getIntData(data, "MaxSteps", vi)) set_maxSteps(Alpaca, vi);
        if (getIntData(data, "maxSpeed", vi)) set_maxSpeed(Alpaca, vi);
        if (getIntData(data, "acceleration", vi)) set_msToMaxSpeed(Alpaca, vi);
        if (getIntData(data, "invertDirection", vi)) set_motorDirection(Alpaca, vi);
    }
    // handle inbound stop
    if (data!=nullptr && data[0]==0) put_halt();
    s.printf( // goto and stop controls
        "<input type=\"radio\" name=\"mon_groupe\" id=\"t2\" class=\"tab-logic\">"
        "<label for=\"t2\" class=\"tab-label\">Position</label>"
        "<div class=\"tab-content\">"
            "<table align=\"center\">"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "  <tr><td align=\"right\"><label for=\"position\">position:</label></td>"
            "      <td><input type=\"text\" id=\"position\" name=\"position\" value=\"%d\"></td>"
            "      <td><input type=\"submit\" value=\"GoTo\"></td></tr>"
            "</form>"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "  <tr><td align=\"right\"><label for=\"Distance\">Distance(mm):</label></td>"
            "      <td><input type=\"text\" id=\"Distance\" name=\"Distance\" value=\"%.3f\"></td>"
            "      <td><input type=\"submit\" value=\"GoTo\"></td></tr>"
            "</form>"
            "</table>"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "  <input type=\"submit\" value=\"Stop\">"
            "</form>"
        "</div>"
        , get_type(), id, get_position(), get_type(), id, float(get_position()*get_stepsize()/1000.0f), get_type(), id);

    s.printf( // saved position table
        "<input type=\"radio\" name=\"mon_groupe\" id=\"t3\" class=\"tab-logic\">"
        "<label for=\"t3\" class=\"tab-label\">Save Pos</label>"
        "<div class=\"tab-content\">"
            "<table align=\"center\">"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "  <tr><td align=\"right\"><label for=\"posname\">save position %d as:</label></td>"
            "      <td><input type=\"text\" id=\"posname\" name=\"posname\" value=\"\"></td>"
            "      <td><input type=\"submit\" value=\"save\"></td></tr>"
            "</form></table>", get_type(), id);
    char *S= getSavedPos(Alpaca); bool hasone= false;
    while (*S!=0)
    {
        char name[20]; int i; for (i=0; i<19; i++) if (*S<=' ') break; else name[i]= *S++; name[i]= 0; if (*S==' ') S++;
        char pos[20]; int j; for (j=0; j<19; j++) if (*S<='!') break; else pos[j]= *S++; pos[j]= 0; if (*S=='!') S++;
        if (i==0 || j==0) break;
        if (!hasone) { s+="<table align=\"center\">"; hasone= true; }
        s.printf("<tr><th><form action=\"/setup/v1/%s/%d/setup\">"
            "  <label for=\"position\">%s:</label>"
            "  <input type=\"text\" id=\"position\" name=\"position\" value=\"%s\">"
            "  <input type=\"submit\" value=\"GoTo\">"
            "</form></th>"
            "<th><form action=\"/setup/v1/%s/%d/setup\">"
            "  <input type=\"hidden\" id=\"erasePos\" name=\"erasePos\" value=\"%s\">"
            "  <input type=\"submit\" value=\"Erase\">"
            "</form></th></tr>"
            , get_type(), id, name, pos, get_type(), id, name);
    }
    if (hasone) s+= "</table>";
    s+= "</div>";

    if (maxSpeed!=-1)
        s.printf( // motor setup
            "<input type=\"radio\" name=\"mon_groupe\" id=\"t4\" class=\"tab-logic\">"
            "<label for=\"t4\" class=\"tab-label\">Motor setup</label>"
            "<div class=\"tab-content\">"
                "<form action=\"/setup/v1/%s/%d/setup\">"
                    "<table align=\"center\">"
                    "  <tr><td align=\"right\"><label for=\"stepSize\">step size in microns:</label></td>"
                    "      <td><input type=\"text\" id=\"stepSize\" name=\"stepSize\" value=\"%.3f\"></td>"
                    "  <tr><td align=\"right\"><label for=\"MaxSteps\">Max Steps:</label></td>"
                    "      <td><input type=\"text\" id=\"MaxSteps\" name=\"MaxSteps\" value=\"%d\"></td>"
                    "  <tr><td align=\"right\"><label for=\"maxSpeed\">max speed in steps/s:</label></td>"
                    "      <td><input type=\"text\" id=\"maxSpeed\" name=\"maxSpeed\" value=\"%d\"></td>"
                    "  <tr><td align=\"right\"><label for=\"acceleration\">time, in ms, to reach full speed:</label></td>"
                    "      <td><input type=\"text\" id=\"acceleration\" name=\"acceleration\" value=\"%d\"></td>"
                    "  <tr><td align=\"right\"><label for=\"invertDirection\">non-0 if inverted direction</label></td>"
                    "      <td><input type=\"text\" id=\"invertDirection\" name=\"invertDirection\" value=\"%d\"></td>"
                    "</table>"
                    "  <input type=\"submit\" value=\"submit\">"
                "</form>"
            "</div>"
            , get_type(), id, get_stepsize(), get_maxstep(), maxSpeed, msToMaxSpeed, motorDirection);
    s+= "<script>document.getElementById('t2').checked = true;</script>";
    if (get_ismoving())
            s.printf("<script> function autoRefresh() { window.location = \"/setup/v1/%s/%d/setup\"; } setInterval('autoRefresh()', 5000);</script>", get_type(), id );
 }

void CFilterWheel::subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s)  // This allows you to add stuff in the HTML or handle inputs...
{
    if (data!=nullptr)
    {   
        char const *d= getStrData(data, "position");
        if (d!=nullptr && *d>='0' && *d<='9') put_position(readInt(d));
        char t[30]; 
        strcpy(t, keyHeader); strcat(t, "names");        d= getStrData(data, "names"); if (d!=nullptr) Alpaca->save(t, getHtmlString(d, names, sizeof(names)));
        strcpy(t, keyHeader); strcat(t, "focusoffsets"); d= getStrData(data, "focusoffsets"); if (d!=nullptr) Alpaca->save(t, getHtmlString(d, focusoffsets, sizeof(focusoffsets)));
    }

    s.printf( // saved positions
        "<input type=\"radio\" name=\"mon_groupe\" id=\"t2\" class=\"tab-logic\">"
        "<label for=\"t2\" class=\"tab-label\">Change Filter</label>"
        "<div class=\"tab-content\">"
        "<form action=\"/setup/v1/%s/%d/setup\">"
        "  <label for=\"position\">position:</label>"
        "  <input type=\"text\" id=\"position\" name=\"position\" value=\"%d\">"
        "  <input type=\"submit\" value=\"Set Wheel\">"
        "  <p>%s</p>"
        "</form>"
        "<h1><a href=\"/setup/v1/%s/%d/setup\">Refresh</a></h1>"
        "</div>"
        ,get_type(), id, get_position(), names, get_type(), id);

    s+= "<script>document.getElementById('t2').checked = true;</script>";

    s.printf( // saved positions
        "<input type=\"radio\" name=\"mon_groupe\" id=\"t3\" class=\"tab-logic\">"
        "<label for=\"t3\" class=\"tab-label\">Filters definition</label>"
        "<div class=\"tab-content\">"
        "<form action=\"/setup/v1/%s/%d/setup\">"
        "<table align=\"center\">"
        "  <tr><td align=\"right\"><label for=\"names\">names (['name1','name2'...]):</label></td>"
        "  <td><input type=\"text\" id=\"names\" name=\"names\" value=\"%s\"></td></tr>"
        "  <tr><td align=\"right\"><label for=\"focusoffsets\">focus offsets ([0,0...]):</label></td>"
        "  <td><input type=\"text\" id=\"focusoffsets\" name=\"focusoffsets\" value=\"%s\"></td></tr>"
        "</table>"
        "  <input type=\"submit\" value=\"Submit\">"
        "</form>"
        "</div>"
        , get_type(), id, names, focusoffsets);
}

// generation of rotator specific setup html...
void CCoverCalibrator::subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s)  // This allows you to add stuff in the HTML or handle inputs...
{
    if (data!=nullptr)
    {   //commands to move to using steps or distance...
        int32_t v= getIntDataDef(data,"calibrateon", -1); if (v!=-1) calibratoron(v);
        v= getIntDataDef(data, "calibratoroff", -1); if (v!=-1) calibratoroff();
        v= getIntDataDef(data, "closecover", -1); if (v!=-1) closecover();
        v= getIntDataDef(data, "opencover", -1); if (v!=-1) opencover();
        v= getIntDataDef(data, "haltcover", -1); if (v!=-1) haltcover();
    }
    uint32_t b, mb; get_brightness(&b); get_maxbrightness(&mb);
    bool moving, changing; get_calibratorchanging(&changing); get_covermoving(&moving);
    CoverState cs; get_coverstate(&cs);
    static char const *css[]={"NotPresent", "Closed", "Moving", "Open", "Unknown", "Error"};
    s.printf(
        "<input type=\"radio\" name=\"mon_groupe\" id=\"t2\" class=\"tab-logic\">"
        "<label for=\"t2\" class=\"tab-label\">change</label>"
        "<div class=\"tab-content\">"
        "<h2>Calibrator%s</h2>" // allows the user to go to position
            "<table align=\"center\">"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "  <tr><td align=\"right\"><label for=\"calibrateon\">brigthness (max:%d):</label></td>"
            "      <td><input type=\"text\" id=\"calibrateon\" name=\"calibrateon\" value=\"%ld\">"
            "      <input type=\"submit\" value=\"Set\"></td></tr>"
            "</form>"
            "</table>"
        "<h2>Cover%s:%s</h2>"
            "<table align=\"center\"><tr><td align=\"center\">"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "  <input type=\"hidden\" id=\"closecover\" name=\"closecover\" value=\"1\"><input type=\"submit\" value=\"closecover\">"
            "</form>"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "  <input type=\"hidden\" id=\"opencover\" name=\"opencover\" value=\"1\"><input type=\"submit\" value=\"opencover\">"
            "</form>"
            "<form action=\"/setup/v1/%s/%d/setup\">"
            "  <input type=\"hidden\" id=\"haltcover\" name=\"haltcover\" value=\"1\"><input type=\"submit\" value=\"haltcover\">"
            "</form>"
            "</td></tr></table>"
        "</div>"
        , changing?" (Changing)":"", get_type(), id, mb, b,
        moving?" (Moving)":"", css[cs], get_type(), id, get_type(), id, get_type(), id);
    s+= "<script>document.getElementById('t2').checked = true;</script>";
}

// generation of Telescope specific setup html...
void CTelescope::subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s)  // This allows you to add stuff in the HTML or handle inputs...
{

    if (data!=nullptr)
    {   
        int v; bool reinit= false;
        if (getIntData(data,"RaMax", v)) { mount.ra.maxPos= v; reinit= true; }
        if (getIntData(data,"RaMaxSpd", v)) { mount.ra.maxSpd= v, reinit= true; }
        if (getIntData(data,"RaAcc", v)) { mount.ra.msToSpd= v, reinit= true; }
        if (getIntData(data,"RaBack", v)) { mount.ra.Backlash= v, reinit= true; }
        if (getIntData(data,"RaSettle", v)) { mount.raSettle= v, reinit= true; }
        if (getIntData(data,"RaAmplitude", v)) { mount.raAmplitude= v, reinit= true; }
        if (getIntData(data,"DecMax", v)) { mount.dec.maxPos= v, reinit= true; }
        if (getIntData(data,"DecMaxSpd", v)) { mount.dec.maxSpd= v, reinit= true; }
        if (getIntData(data,"DecAcc", v)) { mount.dec.msToSpd= v, reinit= true; }
        if (getIntData(data,"DecBack", v)) { mount.dec.Backlash= v, reinit= true; }
        if (getIntData(data,"RaInvert", v)) { mount.ra.invert= v, reinit= true; }
        if (getIntData(data,"DecInvert", v)) { mount.dec.invert= v, reinit= true; }

        if (getIntData(data,"RaGuide", v)) { set_guideraterightascension(v/3600.0f); }
        if (getIntData(data,"DecGuide", v)) { set_guideratedeclination(v/3600.0f); }
        if (reinit) { doReinit(); alpaca->save(keyHeader, "mount", (uint8_t*)&mount, sizeof(mount)); }

    }
    s.printf( // pos, sync goto
        "<input type=\"radio\" name=\"mon_groupe\" id=\"t2\" class=\"tab-logic\">"
        "<label for=\"t2\" class=\"tab-label\">Goto and Sync</label>"
        "<div class=\"tab-content\">"
        "<form action=\"/setup/v1/%s/%d/setup\" method=\"get\">"
        "<table align=\"center\">"
        "<tr><td align=\"center\"><label for=\"ra\">ra in decimal form:</label><input type=\"text\" id=\"ra\" name=\"ra\" value=\"%6f\"></td></tr>"
        "<tr><td align=\"center\"><label for=\"dec\">dec in decimal form:</label><input type=\"text\" id=\"dec\" name=\"dec\" value=\"%6f\"></td></tr>"
        "<tr><td align=\"center\"><button type=\"submit\" name=\"goto\" value=\"0\">goto</button> <button type=\"submit\" name=\"sync\" value=\"0\">sync</button><button type=\"submit\" name=\"stop\" value=\"0\">stop</button></td></tr>"
        "</table></form>"
        "</div>"
        ,get_type(), id, rightascension(), declination());

    s+= "<script>document.getElementById('t2').checked = true;</script>";

    s.printf(
        "<input type=\"radio\" name=\"mon_groupe\" id=\"t3\" class=\"tab-logic\">"
        "<label for=\"t3\" class=\"tab-label\">Motor Setup</label>"
        "<div class=\"tab-content\">"

        "<form action=\"/setup/v1/%s/%d/setup\">"
        "<h2>RA Stepper</h2>"
        "<table align=\"center\">"
        "  <tr><td align=\"right\"><label for=\"RaMax\">Steps for 360deg:</label></td>"
        "      <td><input type=\"text\" id=\"RaMax\" name=\"RaMax\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"RaMaxSpd\">Speed Steps/s:</label></td>"
        "      <td><input type=\"text\" id=\"RaMaxSpd\" name=\"RaMaxSpd\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"RaAcc\">Time to full speed in ms:</label></td>"
        "      <td><input type=\"text\" id=\"RaAcc\" name=\"RaAcc\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"RaBack\">Backlash in steps:</label></td>"
        "      <td><input type=\"text\" id=\"RaBack\" name=\"RaBack\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"RaSettle\">Settle in arcsec:</label></td>"
        "      <td><input type=\"text\" id=\"RaSettle\" name=\"RaSettle\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"RaAmplitude\">Amplitude in degree:</label></td>"
        "      <td><input type=\"text\" id=\"RaAmplitude\" name=\"RaAmplitude\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"RaGuide\">guide rate in arcs/s:</label></td>"
        "      <td><input type=\"text\" id=\"RaGuide\" name=\"RaGuide\" value=\"%.1f\"></td>"
        "  <tr><td align=\"right\"><label for=\"RaInvert\">ra is inverted (0 or 1):</label></td>"
        "      <td><input type=\"text\" id=\"RaInvert\" name=\"RaInvert\" value=\"%d\"></td>"
        "</table>"
        "<h2>Declinaison Stepper</h2>"
        "<table align=\"center\">"
        "  <tr><td align=\"right\"><label for=\"DecMax\">Steps for 360deg:</label></td>"
        "      <td><input type=\"text\" id=\"DecMax\" name=\"DecMax\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"DecMaxSpd\">Speed Steps/s:</label></td>"
        "      <td><input type=\"text\" id=\"DecMaxSpd\" name=\"DecMaxSpd\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"DecAcc\">Time to full speed in ms:</label></td>"
        "      <td><input type=\"text\" id=\"DecAcc\" name=\"DecAcc\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"DecBack\">Backlash in steps:</label></td>"
        "      <td><input type=\"text\" id=\"DecBack\" name=\"DecBack\" value=\"%d\"></td>"
        "  <tr><td align=\"right\"><label for=\"DecGuide\">guide rate in arcs/s:</label></td>"
        "      <td><input type=\"text\" id=\"DecGuide\" name=\"DecGuide\" value=\"%.1f\"></td>"
        "  <tr><td align=\"right\"><label for=\"DecInvert\">dec is inverted (0 or 1):</label></td>"
        "      <td><input type=\"text\" id=\"DecInvert\" name=\"DecInvert\" value=\"%d\"></td>"
        "</table>"
        "<input type=\"submit\" value=\"Update\">"
        "</form>"
        "</div>",

        get_type(), id,
        mount.ra.maxPos, mount.ra.maxSpd, mount.ra.msToSpd, mount.ra.Backlash, mount.raSettle, mount.raAmplitude, get_guideraterightascension()*3600.0f, mount.ra.invert,
        mount.dec.maxPos, mount.dec.maxSpd, mount.dec.msToSpd, mount.dec.Backlash, get_guideratedeclination()*3600.0f, mount.dec.invert);
}

// generation of rotator specific setup html...
void CObservingConditions::subSetup(CAlpaca *Alpaca, int sock, bool get, char *data, CMyStr &s)  // This allows you to add stuff in the HTML or handle inputs...
{
    if (data!=nullptr)
    {   //commands to move to using steps or distance...
        float v= getFloatDataDef(data,"averageperiod", -1.0f); if (v!=-1.0f) put_averageperiod(v/24.0f/60.0f);
        int i= getIntDataDef(data, "refresh", -1); if (v!=-1) put_refresh();
    }
    TAlpacaErr er; float v;

    s+= // list all sensors
    "<input type=\"radio\" name=\"mon_groupe\" id=\"t2\" class=\"tab-logic\">"
    "<label for=\"t2\" class=\"tab-label\">Sensor values</label>"
    "<div class=\"tab-content\">";
    if (get_cloudcover(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>cloud cover: %.2f</h2><br>", v);
    if (get_dewpoint(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>dewpoint: %.2f</h2><br>", v);
    if (get_humidity(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>humidity: %.2f</h2><br>", v);
    if (get_pressure(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>pressure: %.2f</h2><br>", v);
    if (get_rainrate(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_rainrate: %.2f</h2><br>", v);
    if (get_skybrightness(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_skybrightness: %.2f</h2><br>", v);
    if (get_skyquality(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_skyquality: %.2f</h2><br>", v);
    if (get_skytemperature(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_skytemperature: %.2f</h2><br>", v);
    if (get_starfwhm(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_starfwhm: %.2f</h2><br>", v);
    if (get_temperature(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_temperature: %.2f</h2><br>", v);
    if (get_winddirection(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_winddirection: %.2f</h2><br>", v);
    if (get_windgust(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_windgust: %.2f</h2><br>", v);
    if (get_windspeed(&v)==ALPACA_OK) s.printf("<h2 align=\"center/>get_windspeed: %.2f</h2><br>", v);
    s.printf("<table align=\"center\">"
        "<form action=\"/setup/v1/%s/%d/setup\">"
        "  <tr><td align=\"center\"><input type=\"hidden\" id=\"refresh\" name=\"refresh\" value=\"1\"><input type=\"submit\" value=\"refresh\"></td></tr>"
        "</form></table>"
        , get_type(), id);
    s+= "</div>";

    s+= "<script>document.getElementById('t2').checked = true;</script>";

    er= get_averageperiod(&v);
    if (er==ALPACA_OK)
        s.printf(
            "<input type=\"radio\" name=\"mon_groupe\" id=\"t3\" class=\"tab-logic\">"
            "<label for=\"t3\" class=\"tab-label\">refresh rate in minutes</label>"
            "<div class=\"tab-content\">"
                "<form action=\"/setup/v1/%s/%d/setup\">"
                "  <tr><td align=\"right\"><label for=\"averageperiod\">average period:</label></td>"
                "      <td><input type=\"text\" id=\"averageperiod\" name=\"averageperiod\" value=\"%.3f\"></td>"
                "      <td><input type=\"submit\" value=\"Change\"></td></tr>"
                "</form></table>"
            "</div>", get_type(), id, v*24.0f*60.0f);
}

// should you implement mode types, add the sub-setup here! and send them to me (cyrille.de.brebisson@gmail.com) so that they enrich the system!

//////////////////////////////////////
// Persistance stuff...
// Save things in nvs
#ifndef _WIN32
void CAlpaca::save(char const *key, char const *v)
{
    saveLoadBegin();
    nvs_set_blob(saveLoadHandle, key, v, strlen(v)+1); saveLoadHandleDirty= true;
    saveLoadEnd();
}
void CAlpaca::save(char const *key, uint8_t const *v, int size)
{
    saveLoadBegin();
    nvs_set_blob(saveLoadHandle, key, v, size); saveLoadHandleDirty= true;
    saveLoadEnd();
}
void CAlpaca::save(char const *key, int32_t v)
{
    saveLoadBegin();
    nvs_set_i32(saveLoadHandle, key, v); saveLoadHandleDirty= true;
    saveLoadEnd();
}
void CAlpaca::save(char const *key, float v)
{
    int32_t t; memcpy(&t, &v, 4); save(key, t);
}
char *CAlpaca::load(char const *key, char const *def, char *buf, size_t buflen)
{
    saveLoadBegin();
    strncpy2(buf, def, buflen); // copy default in result.
    esp_err_t err= nvs_get_blob(saveLoadHandle, key, buf, &buflen); // does NOT modify buf if error....
    saveLoadEnd();
    return buf;
}
bool CAlpaca::load(char const *key, uint8_t *buf, size_t buflen)
{
    saveLoadBegin();
    size_t size= buflen;
    esp_err_t err= nvs_get_blob(saveLoadHandle, key, buf, &size);
    saveLoadEnd();
    return err==ESP_OK && size==buflen;
}
int32_t CAlpaca::load(char const *key, int32_t def)
{
    saveLoadBegin();
    int32_t v;
    esp_err_t err= nvs_get_i32(saveLoadHandle, key, &v);
    if (err!=ESP_OK) v= def;
    saveLoadEnd(); return v;
}
float CAlpaca::load(char const *key, float def)
{
    int32_t d; memcpy(&d, &def, 4);
    int32_t t= load(key, d);
    memcpy(&def, &t, 4); return def;
}
// these allows to do the "writing" only once if mutliple things change at once...
void CAlpaca::saveLoadBegin()
{
    if (saveLoadCount++!=0) return;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) { ESP_ERROR_CHECK(nvs_flash_erase()); err = nvs_flash_init(); }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nvs_open("Alpaca", NVS_READWRITE, &saveLoadHandle));
    saveLoadHandleDirty= false;
}
void CAlpaca::saveLoadEnd()
{
    if (saveLoadCount==0) return; if (--saveLoadCount!=0) return;
    if (saveLoadHandle==0xffffffff) return;
    if (saveLoadHandleDirty) ESP_ERROR_CHECK(nvs_commit(saveLoadHandle));
    nvs_close(saveLoadHandle);
    saveLoadHandle= 0xffffffff;
}
#else // Here we have a windows impelmentation.. Which does even saves to disk!!!
CPreference* Preference = nullptr;
template <typename T1> static int get(T1 *l, int s, char const *k)
{
    for (int i= 0; i<s; i++) if (strcmp(l[i].k, k)==0) return i;
    strcpy(l[s].k, k);
    return -1;
}
void CAlpaca::save(char const* key, char const* v)
{
    saveLoadBegin();
    int i= get(Preference->texts, Preference->usedText, key); if (i<0) i= Preference->usedText++;
    strcpy(Preference->texts[i].v, v);
    saveLoadHandleDirty = true;
    saveLoadEnd();
}
void CAlpaca::save(char const *key, uint8_t const *v, int size)
{
    saveLoadBegin();
    int i= get(Preference->blobs, Preference->usedBlob, key); if (i<0) i= Preference->usedBlob++;
    Preference->blobs[i].v[0]= size;
    memcpy(Preference->blobs[i].v+1, v, size);
    saveLoadHandleDirty = true;
    saveLoadEnd();
}
void CAlpaca::save(char const* key, int32_t v)
{
    saveLoadBegin();
    int i= get(Preference->ints, Preference->usedInts, key); if (i<0) i= Preference->usedInts++;
    Preference->ints[i].v= v;
    saveLoadEnd();
}
void CAlpaca::save(char const* key, float v) { int32_t t; memcpy(&t, &v, 4); save(key, t); }
char* CAlpaca::load(char const* key, char const* def, char* buf, size_t buflen)
{
    saveLoadBegin(); strcpy(buf, def);
    int i= get(Preference->texts, Preference->usedText, key); 
    if (i>=0) strcpy(buf, Preference->texts[i].v);
    saveLoadEnd();
    return buf;
}
bool CAlpaca::load(char const *key, uint8_t *buf, size_t buflen) // load data in buf. return true if successful
{
    saveLoadBegin();
    int i= get(Preference->blobs, Preference->usedBlob, key); 
    if (i>=0 && Preference->texts[i].v[0]==buflen) memcpy(buf, Preference->texts[i].v+1, buflen);
    saveLoadEnd();
    return buf;
}

int32_t CAlpaca::load(char const* key, int32_t def)
{
    saveLoadBegin();
    int i= get(Preference->ints, Preference->usedInts, key); 
    if (i>=0) def= Preference->ints[i].v;
    saveLoadEnd(); return def;
}
float CAlpaca::load(char const* key, float def)
{
    int32_t d; memcpy(&d, &def, 4);
    int32_t t = load(key, d);
    memcpy(&def, &t, 4); return def;
}
void CAlpaca::saveLoadBegin()
{
    if (saveLoadCount++ != 0) return;
    if (!Preference) Preference = new CPreference;
    Preference->load("ALpaca");
}
void CAlpaca::saveLoadEnd()
{
    if (saveLoadCount == 0) return; if (--saveLoadCount != 0) return;
    if (!saveLoadHandleDirty) return;
    Preference->save("Alpaca");
}
void CPreference::load(char const* fn)
{
    HANDLE h= CreateFileA(fn, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL); if (h==INVALID_HANDLE_VALUE) return;
    DWORD size= GetFileSize(h, NULL); 
    if (size==sizeof(*this))
    {
        uint8_t *data= (uint8_t*)this;
        size_t pos= 0;
        while (size>0) { DWORD read= 0; if (!ReadFile(h, data+pos, size, &read, NULL)) { CloseHandle(h); return; }; size-= read; pos+= read; }
    }
    CloseHandle(h);
}
void CPreference::save(char const* fn)
{
    HANDLE h= CreateFileA(fn, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); if (h==INVALID_HANDLE_VALUE) return;
    DWORD write; WriteFile(h, this, sizeof(*this), &write, NULL);
    CloseHandle(h);
}
#endif

// format for text is: [-]AAA:mm:ss.sss with : that can be replaced by ' and "
char *floatToRa(float ra, char *b)
{
    char *d= b;
    if (ra<0) *d++= '-', ra= -ra;
    int a= int(ra); ra= fmodf(ra, 1.0f)*60.0f;
    int m= int(ra); ra= fmodf(ra, 1.0f)*60.0f;
    sprintf(d, "%d:%d:%.1f", a, m, ra);
    return b;
}
static bool coordToFloat(char const *b, float &v, float min, float max)
{
    bool neg= false; if (*b=='-') neg= true, b++;
    int a, m=0, s= 0; if (!readInt(b, a)) return false;
    if (*b==':' || *b=='"')
    {
        b++; if (!readInt(b, m)) return false;
        if (*b==':' || *b=='\'')
        {
            b++; if (!readInt(b, s)) return false;
            if (*b=='.' ) { b++; readInt(b); }
        }
    }
    if (*b!=0) return false;
    v= (a+m/60.0f+s/3600.0f); if (neg) v= -v;
    return v>=min && v<=max;
}
bool RaToFloat(char const *b, float &ra)  // return true if no error
{
    return coordToFloat(b, ra, 0.0f, 24.0f);
}
bool DecToFloat(char const *b, float &ra) // return true if no error
{
    return coordToFloat(b, ra, -90.0f, 90.0f);
}


#ifndef _WIN32
#include "esp_wifi.h"
#include "esp_event.h"
bool volatile wificonnected= false;
char ipadr[30]="unknow";
// Wifi stuff...
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { esp_wifi_connect(); wificonnected= false; } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
        wificonnected= false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        sprintf(ipadr, IPSTR, IP2STR(&event->ip_info.ip));
        wificonnected= true;
    }
}

void startWifi(char const *net, char const *pass, char const *hostname)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { ESP_ERROR_CHECK(nvs_flash_erase()); ret = nvs_flash_init(); }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif= esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, hostname);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
    wifi_config_t wifi_config; memset(&wifi_config, 0, sizeof(wifi_config));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    memcpy(wifi_config.sta.ssid, net, strlen(net));
    memcpy(wifi_config.sta.password, pass, strlen(pass));
    wifi_config.sta.pmf_cfg.capable= true;
    wifi_config.sta.pmf_cfg.required= false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    if (strlen(net)!=0)
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );
    ESP_LOGI(TAG, "wifi_init_sta finished.");
}
#endif