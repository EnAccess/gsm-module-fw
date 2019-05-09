# EnAccess cellular library

The EnAccess library is an easy to use embedded library for IOT communication using a
cellular modem. It's focus is in transmission of MQTT packages using the
Eclipse Paho MQTT Embedded C/C++ library, but it can be used for general
IP communication as well. The library is platform agnostic and runs either on a
bare metal microcontroller, as well as on top of an embedded OS.
Dialing up the cellular modem, opening an IP channel and sending a MQTT packet
can be done in less than 50 lines of code.

It's easy to add support for a new microcontroller or embedded os. There is also
support for Unix (Linux, OS X) to test code on a PC without to need of having
access to actual microcontroller hardware.

## Supported microcontrollers:
- STM32F1 with HAL

## Supported cellular modems:
- Simcom SIM7x00

## Getting started
The following code shows a simple example for STM32 which dials up the modem,
connects to a host and sends an MQTT packet:
```
int main(int argc, char* argv[])
{
    // System configuration for microcontroller
    System_Config();

    // Set up serial port
    Stm32Uart serial(UART4, GPIOC);

    // Set up modem driver
    Sim7x00CommDevice commDev(serial);

    // Set up task scheduler to call the modem driver's run() function
    Task* taskList[] = {&commDev, NULL};
    Scheduler s(&eTickFunction, taskList);

    // Set up MQTT client
    BlockingCommDevice bld(commDev, eTickFunction, yieldFunction, &s);
    MQTT::Client<BlockingCommDevice, MQTTCountdown> client =
        MQTT::Client<BlockingCommDevice, MQTTCountdown>(bld);

    // Dial up modem and connect to IP host
    commDev.setApn("internet");
    commDev.setHostPort("test.mosquitto.org", 1883);
    commDev.connect();
    while (!commDev.isConnected()) {
        yieldFunction(&s);
    }

    // Connect MQTT client
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = (char*)"enaccess";
    client.connect(data);

    // Send a message
    MQTT::Message message;
    message.qos = MQTT::QOS0;
    message.payload = (void*)"Hello World!";
    message.payloadlen = 13;
    client.publish("enaccess/test", message);

    // Disconnect everything
    client.disconnect();
    commDev.disconnect();
    while (!commDev.isIdle()) {
        yieldFunction(&s);
    }
}
```
See `examples/` directory for full example code.