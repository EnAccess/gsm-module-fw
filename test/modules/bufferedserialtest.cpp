#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#include "bufferedserial.h"

using namespace EnAccess;

TEST_GROUP(BufferedSerialTest)
{
    class BufferedSerialMock : public BufferedSerial
    {
    public:
        BufferedSerialMock()
        { }

        bool open()
        {
            return true;
        }
        void close()
        {
        }

        bool isOpen()
        {
            return true;
        }

        bool setSerialConfig(uint32_t baudRate, uint8_t dataBits)
        {
            return true;
        }

        const char* portName() const
        {
            return NULL;
        }

        uint16_t rawBytesAvailable() const
        {
            return _inBuffer.availableData();
        }

        bool rawRead(uint8_t& data)
        {
            if (!_inBuffer.isEmpty()) {
                data = _inBuffer.pull();
                return true;
            }

            return false;
        }

        virtual bool rawWrite(uint8_t data)
        {
            if (!_outBuffer.isFull()) {
                _outBuffer.push(data);
                return true;
            }

            return false;
        }

        CircularBuffer<char, 120> _inBuffer;
        CircularBuffer<char, 120> _outBuffer;
    };
};

TEST(BufferedSerialTest, TestRead)
{
    BufferedSerialMock bs;
    const uint8_t SIZE = 20;
    char dataIn[SIZE] = "123456789 987654321";
    char dataOut[SIZE];

    bs._inBuffer.push(dataIn, SIZE);

    for (int i = 0; i < 100; i++)
        bs.performReadWrite();

    uint8_t readLen = bs.read(dataOut, SIZE);

    CHECK_EQUAL(SIZE, readLen);
    STRNCMP_EQUAL(dataIn, dataOut, SIZE);
}

TEST(BufferedSerialTest, TestWrite)
{
    BufferedSerialMock bs;
    const uint8_t SIZE = 20;
    char dataIn[SIZE] = "123456789 987654321";
    char dataOut[SIZE];

    bs.write(dataIn, SIZE);

    for (int i = 0; i < 100; i++)
        bs.performReadWrite();

    bs._outBuffer.pull(dataOut, SIZE);

    STRNCMP_EQUAL(dataIn, dataOut, SIZE);
}

TEST(BufferedSerialTest, ShouldReadLines)
{
    BufferedSerialMock bs;
    const uint8_t SIZE = 21;
    char dataIn[SIZE] = "A line\nAnother line\n";
    char dataOut[SIZE];

    bs._inBuffer.push(dataIn, SIZE);

    for (int i = 0; i < 100; i++)
        bs.performReadWrite();

    int outLen = bs.readLine(dataOut, SIZE);
    dataOut[outLen] = '\0';
    STRNCMP_EQUAL("A line\n", dataOut, SIZE);
    outLen = bs.readLine(dataOut, SIZE);
    dataOut[outLen] = '\0';
    STRNCMP_EQUAL("Another line\n", dataOut, SIZE);
}