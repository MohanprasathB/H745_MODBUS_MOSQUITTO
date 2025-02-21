#ifndef INC_MODBUSMASTER_H_
#define INC_MODBUSMASTER_H_
#include <stdint.h>

/* Modbus Master Configuration */
#define MODBUS_MASTER_INBOX_LENGTH         256  // Maximum response size
#define MODBUS_MASTER_OUTBOX_LENGTH        256  // Maximum request size
#define MODBUS_MASTER_TIMEOUT_MS           200  // Response timeout in milliseconds
#define MODBUS_MAX_REGISTERS               128  // Maximum registers to read/write

/* Function Code */
#define MODBUS_FC_READ_INPUT_REGISTERS     0x04  // Read Input Registers

/* RS-485 Line Selection */
#define RS485_TX_ENABLE()                  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_SET)
#define RS485_RX_ENABLE()                  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_RESET)

/* Master-Specific Variables */
extern unsigned char ModbusMaster_inbox[MODBUS_MASTER_INBOX_LENGTH];
extern unsigned char ModbusMaster_outbox[MODBUS_MASTER_OUTBOX_LENGTH];
extern unsigned short ModbusMaster_Tx_index;
extern unsigned short ModbusMaster_Rx_index;
extern unsigned short ModbusMaster_TimeoutFlag;
extern unsigned short ModbusMaster_FrameComplete_Flag;
//extern unsigned char ModbusMaster_outbox[];

/* Structure to Handle Modbus Frames */
typedef struct
{
    union
    {
        unsigned short Data;
        struct
        {
            unsigned short LSB : 8;
            unsigned short MSB : 8;
        } bit;
    } byte;
} Modbus_Split;

typedef struct
{
    union
    {
        unsigned short array[6];
        struct
        {
            unsigned int SlaveID           : 8;  // Slave ID
            unsigned int FunctionCode      : 8;  // Function Code
            unsigned int StartAddress      : 16; // Start Address
            unsigned int DataLength        : 16; // Data Length
            unsigned int crc_value         : 16; // CRC
        } bit;
    } frame;
} Modbus_MasterFrame;

typedef struct {
    const char* tagname;
    uint8_t slave_id;
    uint8_t function_code;
    uint16_t address;
    uint16_t length;
    const char* data_type;
} ModbusTag;

/* Extern Variables */
extern Modbus_MasterFrame ModbusFrame;

/* Function Prototypes */
void ModbusMaster_Init(void);
unsigned short ModbusMaster_CalculateCRC(unsigned char *buffer, unsigned short length);
void ModbusMaster_SendRequest(unsigned char slave_id, unsigned char function_code, unsigned short start_address, unsigned short data_length, unsigned short *data);
unsigned short ModbusMaster_ReceiveResponse(unsigned char *buffer, unsigned short length);
//unsigned char ModbusMaster_ReceiveResponse(unsigned char *buffer, unsigned short length);

// Modify return type and error codes
//uint8_t ModbusMaster_ReceiveResponse(unsigned char *buffer, unsigned short length);
/* Read Input Registers Function */
void ModbusMaster_ReadInputRegisters(unsigned char slave_id, unsigned short start_address, unsigned short data_length);

#endif /* INC_MODBUSMASTER_H_ */
