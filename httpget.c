
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <time.h>

// XDCtools Header files
#include <xdc/runtime/Error.h>
#include <xdc/runtime/System.h>

/* TI-RTOS Header files */
#include <xdc/std.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Swi.h>
#include <ti/sysbios/knl/Queue.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Idle.h>
#include <ti/sysbios/knl/Mailbox.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/drivers/GPIO.h>
#include <ti/net/http/httpcli.h>
#include <math.h>

#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "Board.h"

#include <sys/socket.h>
#include <arpa/inet.h>

#define USER_AGENT        "HTTPCli (ARM; TI-RTOS)"
#define SOCKETTEST_IP     "192.168.1.106"
#define TIME_IP     "128.138.140.44"

#define OUTGOING_PORT     5011
#define INCOMING_PORT     5030
#define TASKSTACKSIZE     4096

extern Semaphore_Handle semaphore1, semaphore2;
extern Swi_Handle swi0;
extern Mailbox_Handle mailbox0;
extern Event_Handle event0;

char tankInfo[1000];
int realtime;
char takenTime[32];
uint32_t ctr=0 ;



Void Timer_ISR(UArg arg1)
{
    Semaphore_post(semaphore1);
    Semaphore_post(semaphore2);
}
Void SWI_ISR()
{

    while(1){
        Semaphore_pend(semaphore1, BIOS_WAIT_FOREVER);
        realtime  = takenTime[0]*16777216 +  takenTime[1]*65536 + takenTime[2]*256 + takenTime[3];
        realtime += 10800;
        realtime += ctr++;
        System_printf("TIME: %s", ctime(&realtime));
        System_flush();

    }
}

Void PinInit(){
    SysCtlClockSet(SYSCTL_SYSDIV_4|SYSCTL_USE_PLL|SYSCTL_XTAL_16MHZ|SYSCTL_OSC_MAIN);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOL);
    SysCtlDelay(10);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOL)){}
    GPIOPinTypeGPIOInput(GPIO_PORTL_BASE, GPIO_PIN_2);
    GPIOPinTypeGPIOOutput(GPIO_PORTL_BASE, GPIO_PIN_3);
    GPIOPadConfigSet(GPIO_PORTL_BASE,GPIO_PIN_2,GPIO_STRENGTH_2MA,GPIO_PIN_TYPE_STD_WPU);
    GPIOIntTypeSet(GPIO_PORTL_BASE,GPIO_PIN_2,GPIO_BOTH_EDGES);
}

Void WaterLevel(UArg arg1, UArg arg2){
    uint32_t pinVal=0;

    while(1){

        PinInit();
        pinVal= GPIOPinRead(GPIO_PORTL_BASE,GPIO_PIN_2);

        if( (pinVal & GPIO_PIN_2)==0){
            strcpy(tankInfo,"water is below a level  ");

        }else{
            strcpy(tankInfo,"water is above a level  ");
        }
        SysCtlPeripheralDisable (SYSCTL_PERIPH_GPIOL);
        Event_post(event0, Event_Id_00);
        Task_sleep(500);
        Mailbox_post(mailbox0, &pinVal, BIOS_NO_WAIT);
        Task_sleep(500);
    }
}


Void SolenoidFxn(UArg arg1, UArg arg2){
    uint32_t pinVal=0;
    while(1){

        Mailbox_pend(mailbox0, &pinVal, BIOS_WAIT_FOREVER);
        PinInit();

        if( (pinVal & GPIO_PIN_2)==0){
            GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_3, GPIO_PIN_3);
            strcpy(tankInfo,"filling the tank  ");
        }
        else{
            GPIOPinWrite(GPIO_PORTL_BASE, GPIO_PIN_3, 0);
            strcpy(tankInfo,"tank is not filling  ");
        }
        Event_post(event0, Event_Id_01);
        SysCtlPeripheralDisable (SYSCTL_PERIPH_GPIOL);
    }
}


/*
 *  ======== printError ========
 */
void printError(char *errString, int code)
{
    System_printf("Error! code = %d, desc = %s\n", code, errString);
    BIOS_exit(code);
}

bool sendData2Server(char *serverIP, int serverPort, char *data, int size)
{
    int sockfd, connStat, numSend;
    bool retval=false;
    struct sockaddr_in serverAddr;

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        System_printf("Socket not created");
        close(sockfd);
        return false;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));  // clear serverAddr structure
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);     // convert port # to network order
    inet_pton(AF_INET, serverIP, &(serverAddr.sin_addr));

    connStat = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if(connStat < 0) {
        System_printf("sendData2Server::Error while connecting to server\n");
    }
    else {
        numSend = send(sockfd, data, size, 0);       // send data to the server
        if(numSend < 0) {
            System_printf("sendData2Server::Error while sending data to server\n");
        }
        else {
            retval = true;
        }
    }
    System_flush();
    close(sockfd);
    return retval;
}

Void clientSocketTask()
{
    char time[64];
    while(1) {
        // wait for the event that httpTask() will signal



        Event_pend(event0, Event_Id_NONE, Event_Id_00+Event_Id_01, BIOS_WAIT_FOREVER);
          sprintf(time, "%s", ctime(&realtime));
          strcat(tankInfo, time);
        GPIO_write(Board_LED0, 1); // turn on the LED

        // connect to SocketTest program on the system with given IP/port

        if(sendData2Server(SOCKETTEST_IP, OUTGOING_PORT, tankInfo, strlen(tankInfo))) {
            System_printf("clientSocketTask:: Informations of the tank are sent to the server\n");
            System_flush();
        }

        GPIO_write(Board_LED0, 0);  // turn off the LED
    }
}
void recvTimeStamptFromNTP(char *serverIP, int serverPort, char *data, int size)
{
        System_printf("recvTimeStamptFromNTP start\n");
        System_flush();

        int sockfd, connStat, tri;
        struct sockaddr_in serverAddr;

        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd == -1) {
            System_printf("Socket not created");
            BIOS_exit(-1);
        }
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(37);
        inet_pton(AF_INET, serverIP , &(serverAddr.sin_addr));

        connStat = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
        if(connStat < 0) {
            System_printf("sendData2Server::Error while connecting to server\n");
            if(sockfd>0) close(sockfd);
            BIOS_exit(-1);
        }

        tri = recv(sockfd, takenTime, sizeof(takenTime), 0);
        if(tri < 0) {
            System_printf("Error while receiving data from server\n");
            if (sockfd > 0) close(sockfd);
            BIOS_exit(-1);
        }
        if (sockfd > 0) close(sockfd);
}
Void socketTask(){

        Semaphore_pend(semaphore2, BIOS_WAIT_FOREVER);

        GPIO_write(Board_LED0, 1);

        recvTimeStamptFromNTP(TIME_IP, 37,realtime, strlen(realtime));

        GPIO_write(Board_LED0, 0);


}

bool createTasks(void)
{
    static Task_Handle taskHandle1,taskHandle2, taskHandle3, taskHandle4;
    Task_Params taskParams;
    Error_Block eb;

    Error_init(&eb);

    Task_Params_init(&taskParams);
    taskParams.stackSize = TASKSTACKSIZE;
    taskParams.priority = 1;
    taskHandle2 = Task_create((Task_FuncPtr)clientSocketTask, &taskParams, &eb);

    Task_Params_init(&taskParams);
    taskParams.stackSize = TASKSTACKSIZE;
    taskParams.priority = 1;
    taskHandle1 = Task_create((Task_FuncPtr)socketTask, &taskParams, &eb);

    Task_Params_init(&taskParams);
    taskParams.stackSize = TASKSTACKSIZE;
    taskParams.priority = 1;
    taskHandle4 = Task_create((Task_FuncPtr)SWI_ISR, &taskParams, &eb);

    if (taskHandle1 == NULL || taskHandle2 == NULL || taskHandle4 == NULL) {
        printError("netIPAddrHook: Failed to create HTTP, Socket and Server Tasks\n", -1);
        return false;
    }

    return true;
}

//  This function is called when IP Addr is added or deleted
//
void netIPAddrHook(unsigned int IPAddr, unsigned int IfIdx, unsigned int fAdd)
{
    // Create a HTTP task when the IP address is added
    if (fAdd) {
        createTasks();
        PinInit();
    }
}

int main(void)
{
    /* Call board init functions */
    Board_initGeneral();
    Board_initGPIO();
    Board_initEMAC();


    /* Turn on user LED */
    GPIO_write(Board_LED0, Board_LED_ON);

    System_printf("Starting the HTTP GET example\nSystem provider is set to "
            "SysMin. Halt the target to view any SysMin contents in ROV.\n");
    /* SysMin will only print to the console when you call flush or exit */



    /* Start BIOS */
    BIOS_start();

    return (0);
}
