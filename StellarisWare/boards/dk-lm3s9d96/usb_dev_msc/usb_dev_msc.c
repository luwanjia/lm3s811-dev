//*****************************************************************************
//
// usb_dev_msc.c - Main routines for the device mass storage class example.
//
// Copyright (c) 2009-2013 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
// 
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
// 
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
// 
// This is part of revision 10636 of the DK-LM3S9D96 Firmware Package.
//
//*****************************************************************************

#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/debug.h"
#include "driverlib/sysctl.h"
#include "driverlib/rom.h"
#include "driverlib/systick.h"
#include "driverlib/usb.h"
#include "driverlib/udma.h"
#include "grlib/grlib.h"
#include "usblib/usblib.h"
#include "usblib/usb-ids.h"
#include "usblib/device/usbdevice.h"
#include "usblib/device/usbdmsc.h"
#include "drivers/kitronix320x240x16_ssd2119_8bit.h"
#include "drivers/set_pinout.h"
#include "usb_msc_structs.h"
#include "third_party/fatfs/src/diskio.h"

//*****************************************************************************
//
//! \addtogroup example_list
//! <h1>USB MSC Device (usb_dev_msc)</h1>
//!
//! This example application turns the evaluation board into a USB mass storage
//! class device.  The application will use the microSD card for the storage
//! media for the mass storage device.  The screen will display the current
//! action occurring on the device ranging from disconnected, no media, reading,
//! writing and idle.
//
//*****************************************************************************

//*****************************************************************************
//
// These defines are used to define the screen constraints to the application.
//
//*****************************************************************************
#define DISPLAY_BANNER_HEIGHT   24
#define DISPLAY_BANNER_BG       ClrDarkBlue
#define DISPLAY_BANNER_FG       ClrWhite

//*****************************************************************************
//
// The number of ticks to wait before falling back to the idle state.  Since
// the tick rate is 100Hz this is approximately 3 seconds.
//
//*****************************************************************************
#define USBMSC_ACTIVITY_TIMEOUT 300

//*****************************************************************************
//
// This enumeration holds the various states that the device can be in during
// normal operation.
//
//*****************************************************************************
volatile enum
{
    //
    // Unconfigured.
    //
    MSC_DEV_DISCONNECTED,

    //
    // Connected but not yet fully enumerated.
    //
    MSC_DEV_CONNECTED,

    //
    // Connected and fully enumerated but not currently handling a command.
    //
    MSC_DEV_IDLE,

    //
    // Currently reading the SD card.
    //
    MSC_DEV_READ,

    //
    // Currently writing the SD card.
    //
    MSC_DEV_WRITE,
}
g_eMSCState;

//*****************************************************************************
//
// The Flags that handle updates to the status area to avoid drawing when no
// updates are required..
//
//*****************************************************************************
#define FLAG_UPDATE_STATUS      1
static unsigned long g_ulFlags;
static unsigned long g_ulIdleTimeout;

//*****************************************************************************
//
// Graphics context used to show text on the display.
//
//*****************************************************************************
tContext g_sContext;

//******************************************************************************
//
// The DMA control structure table.
//
//******************************************************************************
#ifdef ewarm
#pragma data_alignment=1024
tDMAControlTable sDMAControlTable[64];
#elif defined(ccs)
#pragma DATA_ALIGN(sDMAControlTable, 1024)
tDMAControlTable sDMAControlTable[64];
#else
tDMAControlTable sDMAControlTable[64] __attribute__ ((aligned(1024)));
#endif

//*****************************************************************************
//
// Handles bulk driver notifications related to the receive channel (data from
// the USB host).
//
// \param pvCBData is the client-supplied callback pointer for this channel.
// \param ulEvent identifies the event we are being notified about.
// \param ulMsgValue is an event-specific value.
// \param pvMsgData is an event-specific pointer.
//
// This function is called by the bulk driver to notify us of any events
// related to operation of the receive data channel (the OUT channel carrying
// data from the USB host).
//
// \return The return value is event-specific.
//
//*****************************************************************************
unsigned long
RxHandler(void *pvCBData, unsigned long ulEvent,
               unsigned long ulMsgValue, void *pvMsgData)
{
    return(0);
}

//*****************************************************************************
//
// Handles bulk driver notifications related to the transmit channel (data to
// the USB host).
//
// \param pvCBData is the client-supplied callback pointer for this channel.
// \param ulEvent identifies the event we are being notified about.
// \param ulMsgValue is an event-specific value.
// \param pvMsgData is an event-specific pointer.
//
// This function is called by the bulk driver to notify us of any events
// related to operation of the transmit data channel (the IN channel carrying
// data to the USB host).
//
// \return The return value is event-specific.
//
//*****************************************************************************
unsigned long
TxHandler(void *pvCBData, unsigned long ulEvent, unsigned long ulMsgValue,
          void *pvMsgData)
{
    return(0);
}

//*****************************************************************************
//
// This function updates the status area of the screen.  It uses the current
// state of the application to print the status bar.
//
//*****************************************************************************
void
UpdateStatus(char *pcString, tBoolean bClrBackground)
{
    tRectangle sRect;

    //
    //
    //
    GrContextBackgroundSet(&g_sContext, DISPLAY_BANNER_BG);

    if(bClrBackground)
    {
        //
        // Fill the bottom rows of the screen with blue to create the status area.
        //
        sRect.sXMin = 0;
        sRect.sYMin = GrContextDpyHeightGet(&g_sContext) -
                      DISPLAY_BANNER_HEIGHT - 1;
        sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 1;
        sRect.sYMax = sRect.sYMin + DISPLAY_BANNER_HEIGHT;
    
        //
        // Draw the background of the banner.
        //
        GrContextForegroundSet(&g_sContext, DISPLAY_BANNER_BG);
        GrRectFill(&g_sContext, &sRect);

        //
        // Put a white box around the banner.
        //
        GrContextForegroundSet(&g_sContext, DISPLAY_BANNER_FG);
        GrRectDraw(&g_sContext, &sRect);
    }
    else
    {
        //
        // Fill the bottom rows of the screen with blue to create the status area.
        //
        sRect.sXMin = 1;
        sRect.sYMin = GrContextDpyHeightGet(&g_sContext) -
                      DISPLAY_BANNER_HEIGHT;
        sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 2;
        sRect.sYMax = sRect.sYMin + DISPLAY_BANNER_HEIGHT - 2;
    
        //
        // Draw the background of the banner.
        //
        GrContextForegroundSet(&g_sContext, DISPLAY_BANNER_BG);
        GrRectFill(&g_sContext, &sRect);

        //
        // White text in the banner.
        //
        GrContextForegroundSet(&g_sContext, DISPLAY_BANNER_FG);
    }

    //
    // Write the current state to the left of the status area.
    //
    GrContextFontSet(&g_sContext, g_pFontFixed6x8);

    //
    // Update the status on the screen.
    //
    if(pcString != 0)
    {
        GrStringDraw(&g_sContext, pcString, -1, 8, sRect.sYMin + 8, 1);
    }
}

//*****************************************************************************
//
// This function is the call back notification function provided to the USB
// library's mass storage class.
//
//*****************************************************************************
unsigned long
USBDMSCEventCallback(void *pvCBData, unsigned long ulEvent,
                     unsigned long ulMsgParam, void *pvMsgData)
{
    //
    // Reset the time out every time an event occurs.
    //
    g_ulIdleTimeout = USBMSC_ACTIVITY_TIMEOUT;

    switch(ulEvent)
    {
        //
        // Writing to the device.
        //
        case USBD_MSC_EVENT_WRITING:
        {
            //
            // Only update if this is a change.
            //
            if(g_eMSCState != MSC_DEV_WRITE)
            {
                //
                // Go to the write state.
                //
                g_eMSCState = MSC_DEV_WRITE;

                //
                // Cause the main loop to update the screen.
                //
                g_ulFlags |= FLAG_UPDATE_STATUS;
            }

            break;
        }

        //
        // Reading from the device.
        //
        case USBD_MSC_EVENT_READING:
        {
            //
            // Only update if this is a change.
            //
            if(g_eMSCState != MSC_DEV_READ)
            {
                //
                // Go to the read state.
                //
                g_eMSCState = MSC_DEV_READ;

                //
                // Cause the main loop to update the screen.
                //
                g_ulFlags |= FLAG_UPDATE_STATUS;
            }

            break;
        }
        //
        // The USB host has disconnected from the device.
        //
        case USB_EVENT_DISCONNECTED:
        {
            //
            // Go to the disconnected state.
            //
            g_eMSCState = MSC_DEV_DISCONNECTED;

            //
            // Cause the main loop to update the screen.
            //
            g_ulFlags |= FLAG_UPDATE_STATUS;

            break;
        }
        //
        // The USB host has connected to the device.
        //
        case USB_EVENT_CONNECTED:
        {
            //
            // Go to the idle state to wait for read/writes.
            //
            g_eMSCState = MSC_DEV_IDLE;

            break;
        }
        case USBD_MSC_EVENT_IDLE:
        default:
        {
            break;
        }
    }

    return(0);
}

//*****************************************************************************
//
// This is the handler for this SysTick interrupt.  FatFs requires a timer tick
// every 10 ms for internal timing purposes.
//
//*****************************************************************************
void
SysTickHandler(void)
{
    //
    // Call the FatFs tick timer.
    //
    disk_timerproc();

    if(g_ulIdleTimeout != 0)
    {
        g_ulIdleTimeout--;
    }
}

//*****************************************************************************
//
// This is the main loop that runs the application.
//
//*****************************************************************************
int
main(void)
{
    tRectangle sRect;
    unsigned long ulRetcode;

    //
    // Set the clocking to run directly from the crystal.
    //
    ROM_SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
                      SYSCTL_XTAL_16MHZ);

    //
    // Set the device pinout appropriately for this board.
    //
    PinoutSet();

    //
    // Configure SysTick for a 100Hz interrupt.  The FatFs driver wants a 10 ms
    // tick.
    //
    ROM_SysTickPeriodSet(ROM_SysCtlClockGet() / 100);
    ROM_SysTickEnable();
    ROM_SysTickIntEnable();

    //
    // Configure and enable uDMA
    //
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    SysCtlDelay(10);
    ROM_uDMAControlBaseSet(&sDMAControlTable[0]);
    ROM_uDMAEnable();

    //
    // Initialize the display driver.
    //
    Kitronix320x240x16_SSD2119Init();

    //
    // Initialize the graphics context.
    //
    GrContextInit(&g_sContext, &g_sKitronix320x240x16_SSD2119);

    //
    // Fill the top 15 rows of the screen with blue to create the banner.
    //
    sRect.sXMin = 0;
    sRect.sYMin = 0;
    sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 1;
    sRect.sYMax = DISPLAY_BANNER_HEIGHT - 1;
    GrContextForegroundSet(&g_sContext, ClrDarkBlue);
    GrRectFill(&g_sContext, &sRect);

    //
    // Put a white box around the banner.
    //
    GrContextForegroundSet(&g_sContext, ClrWhite);
    GrRectDraw(&g_sContext, &sRect);

    //
    // Put the application name in the middle of the banner.
    //
    GrContextFontSet(&g_sContext, g_pFontCm20);
    GrStringDrawCentered(&g_sContext, "usb-dev-msc", -1,
                         GrContextDpyWidthGet(&g_sContext) / 2, 10, 0);

    //
    // Initialize the idle timeout and reset all flags.
    //
    g_ulIdleTimeout = 0;
    g_ulFlags = 0;

    //
    // Initialize the state to idle.
    //
    g_eMSCState = MSC_DEV_DISCONNECTED;

    //
    // Draw the status bar and set it to idle.
    //
    UpdateStatus("Disconnected", 1);

    //
    // Set the USB stack mode to Device mode with VBUS monitoring.
    //
    USBStackModeSet(0, USB_MODE_DEVICE, 0);

    //
    // Pass our device information to the USB library and place the device
    // on the bus.
    //
    USBDMSCInit(0, (tUSBDMSCDevice *)&g_sMSCDevice);

    //
    // Determine whether or not an SDCard is installed.  If not, print a
    // warning and have the user install one and restart.
    //
    ulRetcode = disk_initialize(0);

    GrContextFontSet(&g_sContext, g_pFontCmss24);
    if(ulRetcode != RES_OK)
    {
        GrStringDrawCentered(&g_sContext, "No SDCard Found", -1,
                             GrContextDpyWidthGet(&g_sContext) / 2,
                             (GrContextDpyHeightGet(&g_sContext) / 2) - 20, 0);
        GrContextFontSet(&g_sContext, g_pFontCm20);
        GrStringDrawCentered(&g_sContext, "Please insert a card and restart.",
                             -1, GrContextDpyWidthGet(&g_sContext) / 2,
                             (GrContextDpyHeightGet(&g_sContext) / 2), 0);
        GrStringDrawCentered(&g_sContext, "Hot-plugging is not supported.",
                             -1, GrContextDpyWidthGet(&g_sContext) / 2,
                             (GrContextDpyHeightGet(&g_sContext) / 2) + 20, 0);
    }
    else
    {
        GrStringDrawCentered(&g_sContext, "SDCard Found",  -1,
                             GrContextDpyWidthGet(&g_sContext) / 2,
                             GrContextDpyHeightGet(&g_sContext) / 2, 0);
    }

    //
    // Drop into the main loop.
    //
    while(1)
    {
        switch(g_eMSCState)
        {
            case MSC_DEV_READ:
            {
                //
                // Update the screen if necessary.
                //
                if(g_ulFlags & FLAG_UPDATE_STATUS)
                {
                    UpdateStatus("Reading", 0);
                    g_ulFlags &= ~FLAG_UPDATE_STATUS;
                }

                //
                // If there is no activity then return to the idle state.
                //
                if(g_ulIdleTimeout == 0)
                {
                    UpdateStatus("Idle", 0);
                    g_eMSCState = MSC_DEV_IDLE;
                }

                break;
            }
            case MSC_DEV_WRITE:
            {
                //
                // Update the screen if necessary.
                //
                if(g_ulFlags & FLAG_UPDATE_STATUS)
                {
                    UpdateStatus("Writing", 0);
                    g_ulFlags &= ~FLAG_UPDATE_STATUS;
                }

                //
                // If there is no activity then return to the idle state.
                //
                if(g_ulIdleTimeout == 0)
                {
                    UpdateStatus("Idle", 0);
                    g_eMSCState = MSC_DEV_IDLE;
                }
                break;
            }
            case MSC_DEV_DISCONNECTED:
            {
                //
                // Update the screen if necessary.
                //
                if(g_ulFlags & FLAG_UPDATE_STATUS)
                {
                    UpdateStatus("Disconnected", 0);
                    g_ulFlags &= ~FLAG_UPDATE_STATUS;
                }
                break;
            }
            case MSC_DEV_IDLE:
            {
                break;
            }
            default:
            {
                break;
            }
        }
    }
}

//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void
__error__(char *pcFilename, unsigned long ulLine)
{
}
#endif

