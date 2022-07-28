// Adapted from https://github.com/adafruit/Adafruit-BMP180-Library
// Written by Limor Fried/Ladyada for Adafruit Industries.

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <time.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/i2c.h>
#include <applibs/log.h>

#include "eventloop_timer_utilities.h"
#include "log_utils.h"

#include "bmp180.h"

#define BMP180_DEBUG 0 // Debug mode

static const uint8_t BMP180_I2CADDR = 0x77; // BMP180 I2C address

static const uint8_t BMP180_ULTRALOWPOWER = 0; // Ultra low power mode
static const uint8_t BMP180_STANDARD = 1;      // Standard mode
static const uint8_t BMP180_HIGHRES = 2;       // High-res mode
static const uint8_t BMP180_ULTRAHIGHRES = 3;  // Ultra high-res mode

static const uint8_t BMP180_CAL_AC1 = 0xAA;    // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_AC2 = 0xAC;    // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_AC3 = 0xAE;    // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_AC4 = 0xB0;    // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_AC5 = 0xB2;    // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_AC6 = 0xB4;    // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_B1 = 0xB6;     // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_B2 = 0xB8;     // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_MB = 0xBA;     // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_MC = 0xBC;     // R   Calibration data (16 bits)
static const uint8_t BMP180_CAL_MD = 0xBE;     // R   Calibration data (16 bits)

static const uint8_t BMP180_CONTROL = 0xF4;         // Control register
static const uint8_t BMP180_TEMPDATA = 0xF6;        // Temperature data register
static const uint8_t BMP180_PRESSUREDATA = 0xF6;    // Pressure data register
static const uint8_t BMP180_READTEMPCMD = 0x2E;     // Read temperature control register value
static const uint8_t BMP180_READPRESSURECMD = 0x34; // Read pressure control register value

static int32_t computeB5(int32_t UT);
static uint8_t read8(uint8_t addr);
static uint16_t read16(uint8_t addr);
static void write8(uint8_t addr, uint8_t data);

static uint8_t oversampling;
static bool initialized = false;

static int16_t ac1, ac2, ac3, b1, b2, mb, mc, md;
static uint16_t ac4, ac5, ac6;

// File descriptors - initialized to invalid value
static int i2cFd = -1;

static datablock_t* dataBlock = NULL;
static EventLoopTimer* i2cTimer = NULL;
static EventLoop* eventLoop = NULL; // not owned

uint8_t read8(uint8_t a) {
    uint8_t ret = 0;

    // send 1 byte, reset i2c, read 1 byte
    I2CMaster_WriteThenRead(i2cFd, BMP180_I2CADDR, &a, 1, &ret, 1);

    return ret;
}

uint16_t read16(uint8_t a) {
    uint8_t retbuf[2] = {0, 0};
    uint16_t ret = 0;

    // send 1 byte, reset i2c, read 2 bytes
    // we could typecast uint16_t as uint8_t array but would need to ensure proper
    // endianness
    I2CMaster_WriteThenRead(i2cFd, BMP180_I2CADDR, &a, 1, retbuf, 2);

    // write_then_read uses uint8_t array
    ret = (uint16_t)(retbuf[1] | (retbuf[0] << 8));

    return ret;
}

void write8(uint8_t a, uint8_t d) {
    const uint8_t sendbuf[2] = {a, d};
    
    // send d prefixed with a (a d [stop])
    I2CMaster_Write(i2cFd, BMP180_I2CADDR, sendbuf, 2);
}

static int32_t computeB5(int32_t UT) {
    int32_t X1 = (UT - (int32_t)ac6) * ((int32_t)ac5) >> 15;
    int32_t X2 = ((int32_t)mc << 11) / (X1 + (int32_t)md);
    return X1 + X2;
}

static void delay(int32_t milliseconds) {
    time_t seconds = milliseconds / 1000;
    int32_t nanoseconds = (milliseconds % 1000) * 1000 * 1000;
    struct timespec sleepTime = { .tv_sec = seconds, .tv_nsec = nanoseconds };
    nanosleep(&sleepTime, NULL);
}

bool bmp180_begin(uint8_t mode) {
    if (mode > BMP180_ULTRAHIGHRES)
        mode = BMP180_ULTRAHIGHRES;
    oversampling = mode;

    i2cFd = I2CMaster_Open(SEEED_MT3620_MDB_J1J2_ISU1_I2C);
    if (i2cFd == -1) {
        Log_Debug("ERROR: I2CMaster_Open: errno=%d (%s)\n", errno, strerror(errno));
        return ExitCode_Init_OpenMaster;
    }

    int result = I2CMaster_SetBusSpeed(i2cFd, I2C_BUS_SPEED_STANDARD);
    if (result != 0) {
        Log_Debug("ERROR: I2CMaster_SetBusSpeed: errno=%d (%s)\n", errno, strerror(errno));
        return ExitCode_Init_SetBusSpeed;
    }

    result = I2CMaster_SetTimeout(i2cFd, 100);
    if (result != 0) {
        Log_Debug("ERROR: I2CMaster_SetTimeout: errno=%d (%s)\n", errno, strerror(errno));
        return ExitCode_Init_SetTimeout;
    }

    if (read8(0xD0) != 0x55)
        return false;

    /* read calibration data */
    ac1 = (int16_t)read16(BMP180_CAL_AC1);
    ac2 = (int16_t)read16(BMP180_CAL_AC2);
    ac3 = (int16_t)read16(BMP180_CAL_AC3);
    ac4 = read16(BMP180_CAL_AC4);
    ac5 = read16(BMP180_CAL_AC5);
    ac6 = read16(BMP180_CAL_AC6);

    b1 = (int16_t)read16(BMP180_CAL_B1);
    b2 = (int16_t)read16(BMP180_CAL_B2);

    mb = (int16_t)read16(BMP180_CAL_MB);
    mc = (int16_t)read16(BMP180_CAL_MC);
    md = (int16_t)read16(BMP180_CAL_MD);
#if (BMP180_DEBUG == 1)
    Log_Debug("ac1 = %d\n", ac1);
    Log_Debug("ac2 = %d\n", ac2);
    Log_Debug("ac3 = %d\n", ac3);
    Log_Debug("ac4 = %d\n", ac4);
    Log_Debug("ac5 = %d\n", ac5);
    Log_Debug("ac6 = %d\n", ac6);

    Log_Debug("b1 = %d\n", b1);
    Log_Debug("b2 = %d\n", b2);

    Log_Debug("mb = %d\n", mb);
    Log_Debug("mc = %d\n", mc);
    Log_Debug("md = %d\n", md);
#endif

    return true;
}

uint16_t bmp180_readRawTemperature(void) {
    write8(BMP180_CONTROL, BMP180_READTEMPCMD);
    delay(5);
    uint16_t raw = read16(BMP180_TEMPDATA);
#if BMP180_DEBUG == 1
    Log_Debug("Raw temp: %d\n", raw);
#endif
    return raw;
}

uint32_t bmp180_readRawPressure(void) {
    uint32_t raw;

    write8(BMP180_CONTROL, (uint8_t)(BMP180_READPRESSURECMD + (oversampling << 6)));

    if (oversampling == BMP180_ULTRALOWPOWER)
        delay(5);
    else if (oversampling == BMP180_STANDARD)
        delay(8);
    else if (oversampling == BMP180_HIGHRES)
        delay(14);
    else
        delay(26);

    raw = read16(BMP180_PRESSUREDATA);

    raw <<= 8;
    raw |= read8((uint8_t)(BMP180_PRESSUREDATA + 2));
    raw >>= (uint32_t)(8 - oversampling);

    /* this pull broke stuff, look at it later?
     if (oversampling==0) {
       raw <<= 8;
       raw |= read8(BMP180_PRESSUREDATA+2);
       raw >>= (8 - oversampling);
     }
    */

#if BMP180_DEBUG == 1
    Log_Debug("Raw pressure: %d\n", raw);
#endif
    return raw;
}

int32_t bmp180_readPressure(void) {
    int32_t UT, UP, B3, B5, B6, X1, X2, X3, p;
    uint32_t B4, B7;

    X1 = 0;
    X2 = 0;

    UT = (int32_t)bmp180_readRawTemperature();
    UP = (int32_t)bmp180_readRawPressure();

#if BMP180_DEBUG == 1
    // use datasheet numbers!
    UT = 27898;
    UP = 23843;
    ac6 = 23153;
    ac5 = 32757;
    mc = -8711;
    md = 2868;
    b1 = 6190;
    b2 = 4;
    ac3 = -14383;
    ac2 = -72;
    ac1 = 408;
    ac4 = 32741;
    oversampling = 0;
#endif

    B5 = computeB5(UT);

#if BMP180_DEBUG == 1
    Log_Debug("X1 = %d\n", X1);
    Log_Debug("X2 = %d\n", X2);
    Log_Debug("B5 = %d\n", B5);
#endif

    // do pressure calcs
    B6 = B5 - 4000;
    X1 = ((int32_t)b2 * ((B6 * B6) >> 12)) >> 11;
    X2 = ((int32_t)ac2 * B6) >> 11;
    X3 = X1 + X2;
    B3 = ((((int32_t)ac1 * 4 + X3) << oversampling) + 2) / 4;

#if BMP180_DEBUG == 1
    Log_Debug("B6 = %d\n", B6);
    Log_Debug("X1 = %d\n", X1);
    Log_Debug("X2 = %d\n", X2);
    Log_Debug("B3 = %d\n", B3);
#endif

    X1 = ((int32_t)ac3 * B6) >> 13;
    X2 = ((int32_t)b1 * ((B6 * B6) >> 12)) >> 16;
    X3 = ((X1 + X2) + 2) >> 2;
    B4 = ((uint32_t)ac4 * (uint32_t)(X3 + 32768)) >> 15;
    B7 = (uint32_t)(UP - B3) * (uint32_t)(50000UL >> oversampling);

#if BMP180_DEBUG == 1
    Log_Debug("X1 = %d\n", X1);
    Log_Debug("X2 = %d\n", X2);
    Log_Debug("B4 = %d\n", B4);
    Log_Debug("B7 = %d\n", B7);
#endif

    if (B7 < 0x80000000) {
        p = (int32_t)((B7 * 2) / B4);
    }
    else {
        p = (int32_t)((B7 / B4) * 2);
    }
    X1 = (p >> 8) * (p >> 8);
    X1 = (X1 * 3038) >> 16;
    X2 = (-7357 * p) >> 16;

#if BMP180_DEBUG == 1
    Log_Debug("p = %d\n", p);
    Log_Debug("X1 = %d\n", X1);
    Log_Debug("X2 = %d\n", X2);
#endif

    p = p + ((X1 + X2 + (int32_t)3791) >> 4);
#if BMP180_DEBUG == 1
    Log_Debug("p = %d\n", p);
#endif
    return p;
}

int32_t bmp180_readSealevelPressure(float altitude_meters) {
    float pressure = (float)bmp180_readPressure();
    return (int32_t)(pressure / pow(1.0 - altitude_meters / 44330, 5.255));
}

float bmp180_readTemperature(void) {
    int32_t UT, B5; // following ds convention
    float temp;

    UT = bmp180_readRawTemperature();

#if BMP180_DEBUG == 1
    // use datasheet numbers!
    UT = 27898;
    ac6 = 23153;
    ac5 = 32757;
    mc = -8711;
    md = 2868;
#endif

    B5 = computeB5(UT);
    temp = (float)((B5 + 8) >> 4);
    temp /= 10;

    return temp;
}

float bmp180_readAltitude(float sealevelPressure) {
    float altitude;

    if (sealevelPressure == 0) {
        sealevelPressure = 101325;
    }
    float pressure = (float)bmp180_readPressure();

    altitude = (float)(44330 * (1.0 - pow(pressure / sealevelPressure, 0.1903)));

    return altitude;
}

static void I2cTimerEventHandler(EventLoopTimer* timer)
{
    if (ConsumeEventLoopTimerEvent(timer) != 0) {
        LogErrno("ERROR: cannot consume the timer event");
        return;
    }

    if (!initialized) {
        initialized = bmp180_begin(BMP180_ULTRAHIGHRES);
    }

    uint32_t pressure = (uint32_t)bmp180_readPressure();
    if (errno == EBUSY || errno == ENXIO) {
        return;
    }
    dataBlock->pressureSamples[dataBlock->pressureSamplesReceived] = pressure;
    dataBlock->pressureSamplesReceived++;
}

ExitCode Bmp180_Init(EventLoop* eventLoopInstance, datablock_t* dataBlockInstance)
{
    eventLoop = eventLoopInstance;
    dataBlock = dataBlockInstance;

    initialized = bmp180_begin(BMP180_ULTRAHIGHRES);
    if (!initialized) {
        Log_Debug("Error: could not initialize the BMP180, will retry\n");
    };

    static const struct timespec pollingInterval = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
    i2cTimer = CreateEventLoopPeriodicTimer(eventLoop, &I2cTimerEventHandler, &pollingInterval);

    if (i2cTimer == NULL) {
        return ExitCode_UploadInit_Timer;
    }

    return ExitCode_Success;
}

void Bmp180_Fini(void)
{
    DisposeEventLoopTimer(i2cTimer);

    Log_Debug("Closing file descriptors.\n");
    CloseFdAndLogOnError(i2cFd, "I2C");
}
