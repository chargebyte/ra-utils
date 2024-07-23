/*====================================================================*
 *   Copyright (c) 2022, 2023, chargebyte GmbH
 *--------------------------------------------------------------------*/

/**
 * @file
 *
 * @brief This service shall update the safety-controller via UART
 * 
 * This implementation follows: Standard_Boot_Firmware.pdf that is stored under:
 * 
 * \\eudeleifile401.smartcharging.global\public\Projekte\Charge_Control_D\10_Documentation\Renesas_Manuals
 * 
 * 
 */

/*====================================================================*
 * system header files;
 *--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <gpiod.h>
#include <termios.h>
#include <fcntl.h>


/*====================================================================*
 * local constants;
 *--------------------------------------------------------------------*/

#define GPIO_CHIP "/dev/gpiochip2"
#define PIN_RESET_SAFETY_UC  "nSAFETY_RESET_INT"
#define PIN_MD_SAFETY_UC     "SAFETY_BOOTMODE_SET"

#define SAFETY_UPDATE_TASK_STACK_SIZE 4096 ///< Task stack size for this module's task
#define UART_RX_BUFFER 512
#define MAXIMUM_NUMBER_OF_DATA_BYTES 1024u
#define MAXIMUM_RETRY_COUNTER 10
#define UART_BAUD_RATE 115200
#define UART_RESPONSE_TIMEOUT_MS 100
#define UART_HW_BUFFER 16

// payload is 1 byte for inquiry command
#define PAYLOAD_LENGTH_INQUIRY 1u
// payload is 5 byte for baud rate command
#define PAYLOAD_LENGTH_BAUD_RATE_COMMAND 5u
// payload is 4 bytes start adress + 4 bytes end adress + 1 byte command = 9 bytes
#define PAYLOAD_LENGTH_WE_COMMAND 9u
// payload is MAXIMUM_NUMBER_OF_DATA_BYTES + 1 byte command
#define PAYLOAD_LENGTH_DATA (MAXIMUM_NUMBER_OF_DATA_BYTES +1u)
// payload is 1 byte response code and 1 byte status code
#define PAYLOAD_LENGTH_RESPONSE 2u
// length of the data packet containing firmware information
// 1 bytes SOD + 2 bytes length + 1 byte response + 32 byte payload + 1 byte checksum + 1 byte EOF
#define LENGTH_DATA_PACKET_FIRMWARE_INFORMATION 38u

static const uint8_t ACK_PATTERN = 0x00;
static const uint8_t GENERIC_CODE_PATTERN = 0x55;
static const uint8_t BOOT_CODE_PATTERN = 0xC3;

static const uint8_t START_OF_DATA_FRAME_PATTERN = 0x81;
static const uint8_t START_OF_COMMAND_FRAME_PATTERN = 0x01;
static const uint8_t END_OF_FRAME_PATTERN = 0x03;

static const uint8_t INQUIRY_COMMAND_PATTERN = 0x00;
static const uint8_t ERASE_COMMAND_PATTERN = 0x12;
static const uint8_t WRITE_COMMAND_PATTERN = 0x13;
static const uint8_t READ_COMMAND_PATTERN = 0x15;
static const uint8_t BAUD_RATE_COMMAND_PATTERN = 0x34;
static const uint8_t STATUS_CODE_OK_PATTERN = 0x00;
static const uint32_t INFO_MAGIC_PATTERN = 0xCAFEBABE;


static const uint32_t CODE_FLASH_START_ADRESS = 0x00000000;
static const uint32_t CODE_FLASH_END_ADRESS =   0x0000FFFF;

static const uint32_t CODE_FIRMWARE_INFORMATION_START_ADRESS =  0x000003E0;
static const uint32_t CODE_FIRMWARE_INFORMATION_END_ADRESS =    0x000003FF;


/*====================================================================*
 * local typedefs;
 *--------------------------------------------------------------------*/

typedef enum
{
    SAFETY_UPDATE_READ,
    SAFETY_UPDATE_WRITE,
    SAFETY_UPDATE_ERASE
} command_types_t;

// inquiry command according to user manual
typedef struct __attribute__((packed))
{
    uint8_t start_of_frame;
    uint16_t length;
    uint8_t command;
    uint8_t sum_data;
    uint8_t end_of_frame;
}inquiry_command_t;

// baud rate command packet
typedef struct __attribute__((packed))
{
    uint8_t start_of_frame;
    uint16_t length;
    uint8_t command;
    uint32_t baud_rate;
    uint8_t sum_data;
    uint8_t end_of_frame;
}baud_rate_command_t;

// structure for read, write and erase command
typedef struct __attribute__((packed))
{
    uint8_t start_of_frame;
    uint16_t length;
    uint8_t command;
    uint32_t start_adress;
    uint32_t end_adress;
    uint8_t sum_data;
    uint8_t end_of_frame;
}read_write_erase_command_t;

// response packet according to user manual
typedef struct __attribute__((packed))
{
    uint8_t start_of_frame;
    uint16_t length;
    uint8_t response;
    uint8_t status_code;
    uint8_t sum_data;
    uint8_t end_of_frame;
}response_t;

// data packet according to user manual
typedef struct __attribute__((packed))
{
    uint8_t start_of_frame;
    uint16_t length;
    uint8_t response;
    uint8_t data[MAXIMUM_NUMBER_OF_DATA_BYTES];
    uint8_t sum_data;
    uint8_t end_of_frame;
}data_packet_t;

/**
 * @brief Structure of the application validation block.
 *
 * This structure contains all information which are necessary to check if the code of an application
 * is valid and can be executed.
 */
typedef struct version_app_infoblock_t
{
    uint32_t                start_magic_pattern;   ///< Magic pattern to ensure, that this block is valid
    uint32_t                application_size;     ///< Size of the application
    uint32_t                application_checksum; ///< Checksum (CRC32) of the application
    uint8_t                 sw_major_version;      ///< Software major version
    uint8_t                 sw_minor_version;      ///< Software minor version
    uint8_t                 sw_build_version;      ///< Software build version
    uint64_t                git_hash;             ///< Git hash of the HEAD used to build this SW
    uint8_t                 reserved[5];         ///< 5 bytes for future use
    uint32_t                end_magic_pattern;     ///< Magic pattern to ensure, that this block is valid
} __attribute__((packed)) version_app_infoblock_t;

/*====================================================================*
 * local data;
 *--------------------------------------------------------------------*/

static uint8_t g_rx_buffer[UART_RX_BUFFER] = {0x00};
static data_packet_t g_data_packet;

static version_app_infoblock_t g_firware_info = {0x00};
static version_app_infoblock_t g_stored_firware_info = {0x00};
static uint8_t g_update_progress = 0u;

/*====================================================================*
 * local functions declaration
 *--------------------------------------------------------------------*/

/**
 * @brief Get hash, version info and other data from the firmware file on the file system
 * @param filename: The filename of the firmware file
 * 
 * @return -1 error, 0 success
 */
static int safety_update_get_stored_firmware_information(const char *filename);
static void safety_update_update_reset_to_bootmode(struct gpiod_line_request *line_request, 
                                                    int                       line_reset,
                                                    int                       line_md);

 
static void safety_update_update_reset_to_normal_mode(struct gpiod_line_request *line_request, 
                                                       int                       line_reset,
                                                       int                       line_md);                                                   /**
 * @brief Initialize tue UART communication. After this function is called. We know that the chip is accepting commands
 * 
 * @return Indication of successfull inizialization
 */
static int safety_update_initialize_communication(int* uart_stream);

/**
 * @brief Checks if the data are correct: length and checksum
 * 
 * @param response_t* Pointer to the response to be checked
 * 
 * @return true if everything is ok, false otherwise
 */
static bool safety_update_check_response(response_t* response);

/**
 * @brief Adds the checksum to the baud rate setting command
 * 
 * @param baud_rate_command_t* Pointer to the command
 */
static void safety_update_add_checksum_to_baud_rate_command(baud_rate_command_t* command);

/**
 * @brief Adds the checksum to the read/write/erase command
 * 
 * @param read_write_erase_command_t* Pointer to the command
 */
static void safety_update_add_checksum_to_rwe_command(read_write_erase_command_t* command);

/**
 * @brief Adds the checksum to the data packet
 */
static void safety_update_add_checksum_to_data_packet();

/**
 * @brief Initialize the GPIOs to get a handle and offset
 * 
 * @param gpiod_line_request* The request handle
 * @param int* Offset for the reset line
 * @param int* Offset for the mode device line
 */
static void safety_update_init_gpios( struct gpiod_line_request **line_request, 
                                      int                       *line_reset,
                                      int                       *line_md);

/**
 * @brief Reads the firmware information from the chip and stores in in g_firmware_information
 * 
 * @return Indication of successfull read procedure
 */
static int safety_update_read_firmware_information(int uart_stream);

/**
 * @brief transmits the write/erase command to the chip
 * 
 *  Depending on the we_flag, this function transmits a write or erase command to the chip
 *  and checks for the response
 * 
 * @param start_adress adress to start the write or erase procedure
 * @param end_adress  adress to end the write or erase procedure
 * @param we_flag  true for erase command, false for write command
 * 
 * @return Indication of successfull transmission of command
 */
static int safety_update_transmit_rwe_command(int uart_stream, uint32_t start_adress, uint32_t end_adress, command_types_t rwe_command);

/**
 * @brief Reads the binary data from a file and writes it to the chip on the corresponding adress
 * 
 * @return Indication of successfull write procedure
 */
static int safety_update_write_firmware(int uart_stream, const char *filename);

static ssize_t read_uart_buffer(int fd, uint8_t* buf, size_t buffer_size);

/**
 * @brief Fill data packet from firmware binary file
 * 
 * @return true if there are bytes left to read, false if you are done
 */
static bool safety_update_fill_data_packet(FILE * file_pointer);

/**
 * @brief transmits data packets to be written on the chip
 * 
 * @return Indication of successfull transmission of data
 */

static int safety_update_transmit_data_packet(int uart_stream);



void print_help() {
    printf("Usage: renesas_updater [OPTIONS]\n");
    printf("Options:\n");
    printf("  -u            Update\n");
    printf("  -f <filename> Specify file for update or information\n");
    printf("  -h            Display this help message\n");
}

int main(int argc, char *argv[]) {
    int opt;
    char *filename = NULL;
    bool update_flag = false;

    struct gpiod_line_request *line_request;
    int line_reset;
    int line_md;
    int uart_stream;

    if (argc < 1) {
        print_help();
        return 1;
    }

    while ((opt = getopt(argc, argv, "uf:h")) != -1) {
        switch (opt) {
            case 'u':
                update_flag = true;
                break;
            case 'f':
                filename = optarg;
                break;
            case 'h':
                print_help();
                break;
            default:
                print_help();
                return 1;
        }
    }

    if (filename)
    {
        if(safety_update_get_stored_firmware_information(filename) < 0)
        {
            fprintf(stderr, "Not able to get stored firmware information\n");
            return -1;
        }
    }

    safety_update_init_gpios( &line_request, &line_reset, &line_md);
    // Set the chip to boot mode
    safety_update_update_reset_to_bootmode(line_request, line_reset, line_md);
    
    // First we have to initialize the system and check if we get an answer
    if(safety_update_initialize_communication(&uart_stream) < 0)
    {
        fprintf(stderr, "Not able to initialize communication\n");
        safety_update_update_reset_to_normal_mode(line_request, line_reset, line_md);
        return -1;
    }

    // Read out information stored on system
    if(safety_update_read_firmware_information(uart_stream) < 0)
    {
        fprintf(stderr, "Not able to read out firmware information\n");
        safety_update_update_reset_to_normal_mode(line_request, line_reset, line_md);
        return -1;
    }

    if(update_flag && filename)
    {
        // Next step is to erase the flash
        if(safety_update_transmit_rwe_command(uart_stream, CODE_FLASH_START_ADRESS, CODE_FLASH_END_ADRESS, SAFETY_UPDATE_ERASE) < 0)
        {
            fprintf(stderr, "safety_update_erase_flash failed\n");
            safety_update_update_reset_to_normal_mode(line_request, line_reset, line_md);
            return -1;
        }

        // Write the firmware down to flash
        // Here we will stay for about a minute
        if(safety_update_write_firmware(uart_stream, filename) < 0)
        {
            fprintf(stderr, "safety_update_write_firmware failed\n");
            safety_update_update_reset_to_normal_mode(line_request, line_reset, line_md);
            return -1;
        }
    }

    safety_update_update_reset_to_normal_mode(line_request, line_reset, line_md);
    return 0;
}




static int safety_update_get_stored_firmware_information(const char *filename)
{
    size_t bytes_read = 0u;
    FILE *firmware = fopen (filename,"rb");
    if(firmware == NULL)
    {
        fprintf(stderr, "Not able to open file: %s",filename );
        return -1;
    }

    fseek(firmware, CODE_FIRMWARE_INFORMATION_START_ADRESS, SEEK_SET);

    bytes_read = fread(&g_stored_firware_info, sizeof(uint8_t), sizeof(g_stored_firware_info), firmware);
    if(bytes_read != sizeof(g_stored_firware_info))
    {
        fprintf(stderr, "%s: Read bytes: %d vs sizeof(g_stored_firware_info): %d",filename, bytes_read, sizeof(g_stored_firware_info) );
        fclose(firmware);
        return -1; 
    }

    fclose(firmware);

    // If the start or end pattern is not correct ... we wipe the data
    if(g_stored_firware_info.start_magic_pattern != INFO_MAGIC_PATTERN)
    {
        fprintf(stderr, "%s: start_magic_pattern: 0x%08X is not as expected (0x%08X)", filename, (unsigned int)g_stored_firware_info.start_magic_pattern, (unsigned int)INFO_MAGIC_PATTERN) ;
        memset(&g_stored_firware_info, 0x00, sizeof(version_app_infoblock_t));
        return -1; 
    }
    if(g_stored_firware_info.end_magic_pattern != INFO_MAGIC_PATTERN)
    {
        fprintf(stderr, "%s: end_magic_pattern: 0x%08X is not as expected (0x%08X)", filename, (unsigned int)g_stored_firware_info.end_magic_pattern, (unsigned int)INFO_MAGIC_PATTERN) ;
        memset(&g_stored_firware_info, 0x00, sizeof(version_app_infoblock_t));
        return -1; 
    }

    printf("%s firmware_size: %d\n", filename, (unsigned int)g_stored_firware_info.application_size);
    printf("%s git_hash: 0x%016llX\n", filename, (unsigned long long int)g_stored_firware_info.git_hash);
    printf("%s SW: %d.%d.%d\n", filename, (unsigned int)g_stored_firware_info.sw_major_version, (unsigned int)g_stored_firware_info.sw_minor_version, (unsigned int)g_stored_firware_info.sw_build_version);

    return 0; 

}


static void safety_update_init_gpios( struct gpiod_line_request **line_request, 
                                      int                       *line_reset,
                                      int                       *line_md)
{
    struct gpiod_chip *chip;
    struct gpiod_chip_info *chip_info;
    struct gpiod_line_config *line_config;
    struct gpiod_line_settings *line_settings;

    int num_lines;
    int line_offset;

    chip = gpiod_chip_open(GPIO_CHIP);

    *line_reset = gpiod_chip_get_line_offset_from_name(chip, PIN_RESET_SAFETY_UC);
    *line_md =    gpiod_chip_get_line_offset_from_name(chip, PIN_MD_SAFETY_UC);

    line_config = gpiod_line_config_new();
    line_settings = gpiod_line_settings_new();

    // Set the line settings to output
    gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_config_add_line_settings(line_config, line_reset, 1, line_settings);
    gpiod_line_config_add_line_settings(line_config, line_md, 1, line_settings);
  
    *line_request = gpiod_chip_request_lines(chip, NULL, line_config);
}

static void safety_update_update_reset_to_bootmode( struct gpiod_line_request *line_request, 
                                                    int                       line_reset,
                                                    int                       line_md)
{

    // Reset the controller
    gpiod_line_request_set_value(line_request, line_reset, GPIOD_LINE_VALUE_INACTIVE);
    // Enable boot mode by setting MD to LOW
    gpiod_line_request_set_value(line_request, line_md, GPIOD_LINE_VALUE_INACTIVE);

    // Let's stay 500ms in reset
    usleep(500000);

    // Come up againg
    gpiod_line_request_set_value(line_request, line_reset, GPIOD_LINE_VALUE_ACTIVE);
    
}

static void safety_update_update_reset_to_normal_mode( struct gpiod_line_request *line_request, 
                                                       int                       line_reset,
                                                       int                       line_md)
{


    // Reset the controller
    gpiod_line_request_set_value(line_request, line_reset, GPIOD_LINE_VALUE_INACTIVE);
    // Enable boot mode by setting MD to LOW
    gpiod_line_request_set_value(line_request, line_md, GPIOD_LINE_VALUE_ACTIVE);

    // Let's stay 500ms in reset
    usleep(500000);
    
    // Come up againg
    gpiod_line_request_set_value(line_request, line_reset, GPIOD_LINE_VALUE_ACTIVE);
    
}

static int safety_update_initialize_communication(int *uart_stream)
{

    // Give the controller some time to start up
    usleep(500000);

    // Open the UART device file
    *uart_stream = open("/dev/ttyLP2", O_RDWR | O_NOCTTY | O_NDELAY);
    if (*uart_stream == -1)
    {
        printf("Error - Unable to open UART.\n");
        return -1;
    }

    // Configure the UART
    fcntl(*uart_stream, F_SETFL, 0); // Set the file status flags to blocking mode
    struct termios options;
    tcgetattr(*uart_stream, &options);
    options.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
    options.c_iflag = IGNPAR;
    options.c_oflag = 0;
    options.c_lflag = 0;
    options.c_cc[VTIME] = UART_RESPONSE_TIMEOUT_MS/100;    // Timeout in deciseconds
    options.c_cc[VMIN] = 0;
    tcflush(*uart_stream, TCIFLUSH);
    tcsetattr(*uart_stream, TCSANOW, &options);


    // Send out 2 times 0x00 to start communication
    write(*uart_stream, &ACK_PATTERN, sizeof(ACK_PATTERN));
    usleep(100000);
    write(*uart_stream, &ACK_PATTERN, sizeof(ACK_PATTERN));

    // After 2 times there should be a response from the host
    int received_bytes = read_uart_buffer(*uart_stream, g_rx_buffer, UART_RX_BUFFER);

    int timeout_counter = 0;
    
    // if no response arrived, repeat the 0x00 pattern until data arrived
    while((received_bytes <= 0) && (timeout_counter < MAXIMUM_RETRY_COUNTER))
    {
        fprintf(stderr,  "received_bytes: %i, timeout_counter: %i\n",received_bytes, timeout_counter) ;
        tcflush(*uart_stream, TCIFLUSH);
        write(*uart_stream, &ACK_PATTERN, sizeof(ACK_PATTERN));
        received_bytes = read_uart_buffer(*uart_stream, g_rx_buffer, UART_RX_BUFFER);
        timeout_counter ++;
    }

    if(timeout_counter >= MAXIMUM_RETRY_COUNTER)
    {
        fprintf(stderr,  "No ACK from Renesas received\n") ;
        return -1;
    }

    if(received_bytes != 1)
    {
       fprintf(stderr, "Init 0x00: Wrong length, Expected : 1, Received: %d", received_bytes);
        return -1;
    }

    if(g_rx_buffer[0] != ACK_PATTERN)
    {
        fprintf(stderr, "Wrong content, Expected : 0x00, Received: 0x%2x", g_rx_buffer[0]);
        return -1;
    }

    tcflush(*uart_stream, TCIFLUSH);

    // According to the manual we should now send our 0x55
   write(*uart_stream, &GENERIC_CODE_PATTERN, sizeof(GENERIC_CODE_PATTERN));

    // With the reception of 0xC3 the controller is ready to handle commands
    received_bytes = read_uart_buffer(*uart_stream, g_rx_buffer, sizeof(BOOT_CODE_PATTERN));

    if(received_bytes != sizeof(BOOT_CODE_PATTERN))
    {
        fprintf(stderr, "Init 0x55: Wrong length, Expected : 1, Received: %d", received_bytes);
        return -1;
    }

    if(g_rx_buffer[0] != BOOT_CODE_PATTERN)
    {
        fprintf(stderr, "Wrong content, Expected : 0xC3, Received: 0x%2x", g_rx_buffer[0]);
        return -1;
    }

    // The manual proposes to send an inquiry command now and check for the correct response
    inquiry_command_t inquiry_command;
    response_t response;

    inquiry_command.start_of_frame = START_OF_COMMAND_FRAME_PATTERN;
    inquiry_command.length         = __bswap_16(PAYLOAD_LENGTH_INQUIRY);
    inquiry_command.command        = INQUIRY_COMMAND_PATTERN;
    inquiry_command.sum_data       = 0xff; // no need to calculate that ... data are fix
    inquiry_command.end_of_frame   = END_OF_FRAME_PATTERN;
    
    write(*uart_stream, &inquiry_command, sizeof(inquiry_command));
  
    // Now we check for the response from the host
    received_bytes = read_uart_buffer(*uart_stream, g_rx_buffer, sizeof(response));

    if(received_bytes != sizeof(response))
    {
        fprintf(stderr, "Expected length of inquiry request: %d, vs received %d", sizeof(response), received_bytes);
        return -1;
    }

    memcpy(&response, g_rx_buffer, sizeof(response));

    if(safety_update_check_response(&response) == false)
    {
        fprintf(stderr, "Failed inquiry response\n");
        return -1;
    }

    // Response data is correct, now check for the content
    if((response.response != INQUIRY_COMMAND_PATTERN) || (response.status_code != STATUS_CODE_OK_PATTERN))
    {
        fprintf(stderr, "Error in inquiry response RES: 0x%2X, STS 0x%2X", response.response, response.status_code);
        return -1;
    }

    // Here we change the baud rate from 9600 to UART_BAUD_RATE
    baud_rate_command_t baud_rate_command;
    memset(&response, sizeof(response_t), sizeof(uint8_t));

    baud_rate_command.start_of_frame = START_OF_COMMAND_FRAME_PATTERN;
    baud_rate_command.length         = __bswap_16(PAYLOAD_LENGTH_BAUD_RATE_COMMAND);
    baud_rate_command.command        = BAUD_RATE_COMMAND_PATTERN;
    baud_rate_command.baud_rate      = __bswap_32(UART_BAUD_RATE);
    baud_rate_command.end_of_frame   = END_OF_FRAME_PATTERN;

    safety_update_add_checksum_to_baud_rate_command(&baud_rate_command);

    write(*uart_stream, &baud_rate_command, sizeof(baud_rate_command));

    // Now we check for the response from the host
    received_bytes = read_uart_buffer(*uart_stream, g_rx_buffer, sizeof(response));

    if(received_bytes != sizeof(response))
    {
        fprintf(stderr, "Expected length of baud rate response: %d, vs received %d", sizeof(response), received_bytes);
        return -1;
    }

    memcpy(&response, g_rx_buffer, sizeof(response));

    if(safety_update_check_response(&response) == false)
    {
        fprintf(stderr, "Failed baud rate response\n");
        return -1;
    }

    if(response.response != BAUD_RATE_COMMAND_PATTERN)
    {
        fprintf(stderr, "Response to baud rate setting: RES: 0x%02x, STS: 0x%02x", response.response, response.status_code);
    }

    tcgetattr(*uart_stream, &options);
    options.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    options.c_iflag = IGNPAR;
    options.c_oflag = 0;
    options.c_lflag = 0;
    options.c_cc[VMIN] = 0;
    // Todo: This is not working or I dont understand it:
    // according to the documentation the timeout should be in deciseconds
    // This means 1 represents 100ms
    // However ... in my measurements 10 corresponds to 100ms and 1 to 10ms
    options.c_cc[VTIME] = 10;
    tcflush(*uart_stream, TCIFLUSH);
    tcsetattr(*uart_stream, TCSANOW, &options);

    usleep(10000);
    
    write(*uart_stream, &inquiry_command, sizeof(inquiry_command));
  
    // Now we check for the response from the host
    received_bytes = read_uart_buffer(*uart_stream, g_rx_buffer, sizeof(response));

    if(received_bytes != sizeof(response))
    {
        fprintf(stderr, "Expected length of inquiry request: %d, vs received %d", sizeof(response), received_bytes);
        return -1;
    }

    memcpy(&response, g_rx_buffer, sizeof(response));

    if(safety_update_check_response(&response) == false)
    {
        fprintf(stderr, "Failed inquiry response\n");
        return -1;
    }

    // Response data is correct, now check for the content
    if((response.response != INQUIRY_COMMAND_PATTERN) || (response.status_code != STATUS_CODE_OK_PATTERN))
    {
        fprintf(stderr, "Error in inquiry response RES: 0x%2X, STS 0x%2X", response.response, response.status_code);
        return -1;
    }

    return 0;
}


static bool safety_update_check_response(response_t* response)
{
    // 2 byte length + 1 byte response code + 1 byte status code
    const uint8_t sum_size = 4u;
    uint8_t sum = 0x00;
    uint8_t* pointer_response;

    // Check the pointer first
    if(response == NULL)
    {
        fprintf(stderr, "safety_update_check_response --> Invalid pointer\n");
        return false;
    }

    // We map that to a uint8 pointer
    pointer_response = (uint8_t *)response;

    if(response->start_of_frame != START_OF_DATA_FRAME_PATTERN)
    {
        fprintf(stderr, "Expected start_of_data_frame_pattern: %d, vs received %d", START_OF_DATA_FRAME_PATTERN, response->start_of_frame);
        return false;
    }

    if(response->length != __bswap_16(PAYLOAD_LENGTH_RESPONSE))
    {
        fprintf(stderr, "Expected data length: 2, vs received %d", response->length);
       return false;
    }

    // First byte is the start byte ... this needs to be skipped
    pointer_response ++;

    // Add sum over the next 4 bytes
    for(uint8_t i = 0; i< sum_size; i++)
    {
        sum += (*pointer_response);
        pointer_response ++;
    }

    if((uint8_t)(sum + response->sum_data) != 0x00)
    {
        fprintf(stderr, "Calculated sum: 0x%2X, vs received 0x%2X", sum, response->sum_data);
        return false;
    }

    return true;

}


static void safety_update_add_checksum_to_rwe_command(read_write_erase_command_t* command)
{
    // 2 byte length + 1 byte command + 4 byte start adress + 4 byte end adress
    const uint8_t sum_size = 11u;
    uint8_t sum = 0x00;
    uint8_t* pointer_command;

    // We map that to a uint8 pointer
    pointer_command = (uint8_t *)command;

    // First byte is the start byte ... this needs to be skipped
    pointer_command ++;

    // Add sum over the next 11 bytes
    for(uint8_t i = 0; i< sum_size; i++)
    {
        sum += (*pointer_command);
        pointer_command ++;
    }

    // Calculate 2s complemnent
    command->sum_data = (uint8_t)  (0x00 - sum);
}


static void safety_update_add_checksum_to_baud_rate_command(baud_rate_command_t* command)
{
    uint8_t sum = 0x00;
    uint8_t* pointer_command;

    // We map that to a uint8 pointer
    pointer_command = (uint8_t *)command;

    // First byte is the start byte ... this needs to be skipped
    pointer_command ++;

    // Add sum over 5 bytes payload + 2 bytes length field
    for(uint8_t i = 0; i< PAYLOAD_LENGTH_BAUD_RATE_COMMAND + 2; i++)
    {
        sum += (*pointer_command);
        pointer_command ++;
    }

    // Calculate 2s complemnent
    command->sum_data = (uint8_t)  (0x00 - sum);
}

static int safety_update_read_firmware_information(int uart_stream)
{
    int retValue;

    retValue = safety_update_transmit_rwe_command(uart_stream, CODE_FIRMWARE_INFORMATION_START_ADRESS, CODE_FIRMWARE_INFORMATION_END_ADRESS, SAFETY_UPDATE_READ);
    if(retValue != 0)
    {
        return retValue;
    }

    int received_bytes = read_uart_buffer(uart_stream, g_rx_buffer, LENGTH_DATA_PACKET_FIRMWARE_INFORMATION);

    // Check if we have received the right amount of bytes
    if(received_bytes != LENGTH_DATA_PACKET_FIRMWARE_INFORMATION)
    {
        fprintf(stderr,  "safety_update_read_firmware_information failed. Only %d packets received\n", received_bytes);
        return -1;
    }

    // Check for start of data pattern
    if(g_rx_buffer[0] != START_OF_DATA_FRAME_PATTERN)
    {
        fprintf(stderr,  "safety_update_read_firmware_information failed. SOD: 0x%02X Expected 0x81\n", g_rx_buffer[0]);
        return -1;
    }

    // Check for correct payload length
    uint16_t payload_length = g_rx_buffer[2] + (g_rx_buffer[1] >> 8);

    // + 1 due to the Response byte
    if(payload_length != sizeof(version_app_infoblock_t) + 1)
    {
        fprintf(stderr,  "safety_update_read_firmware_information failed. payload_length: %d Expected: %d\n",payload_length, (sizeof(version_app_infoblock_t) + 1));
        return -1;
    }

    // Check for correct response byte
    if(g_rx_buffer[3] != READ_COMMAND_PATTERN)
    {
        fprintf(stderr,  "safety_update_read_firmware_information failed. RES: 0x%02X Expected: 0x%02X\n", g_rx_buffer[3], READ_COMMAND_PATTERN);
        return -1;
    }


    uint8_t sum = 0x00;
    uint8_t i;
    // Add sum over everything but SOF and EOF ... so we start at 1 and end -1 bytes
    for(i = 1; i < (received_bytes-1); i++)
    {
        sum += g_rx_buffer[i];
    }

    // The sum over payload (including sum field) must result in 0x00
    if(sum != 0x00)
    {
        fprintf(stderr,  "Calculated sum: 0x%2X, vs expected 0x00\n", sum);
        return -1;
    }

    memcpy(&g_firware_info, &g_rx_buffer[4], sizeof(version_app_infoblock_t));

    // If the start or end pattern is not correct ... we wipe the data
    // We still give back an ESP_OK cause the read out worked
    // But there was no valid data on the Renesas itself
    if(g_firware_info.start_magic_pattern != INFO_MAGIC_PATTERN)
    {
        printf("start_magic_pattern: 0x%08X is not as expected (0x%08X)\n",(unsigned int)g_firware_info.start_magic_pattern, (unsigned int)INFO_MAGIC_PATTERN) ;
        memset(&g_firware_info, 0x00, sizeof(version_app_infoblock_t));
    }
    if(g_firware_info.end_magic_pattern != INFO_MAGIC_PATTERN)
    {
        printf("end_magic_pattern: 0x%08X is not as expected (0x%08X)\n",(unsigned int)g_firware_info.end_magic_pattern, (unsigned int)INFO_MAGIC_PATTERN) ;
        memset(&g_firware_info, 0x00, sizeof(version_app_infoblock_t));
    }

    printf("firmware_size: %d\n", (unsigned int)g_firware_info.application_size);
    printf("git_hash: 0x%016llX\n", (unsigned long long int)g_firware_info.git_hash);
    printf("SW: %d.%d.%d\n", (unsigned int)g_firware_info.sw_major_version, (unsigned int)g_firware_info.sw_minor_version, (unsigned int)g_firware_info.sw_build_version);

    return 0;

}



static int safety_update_transmit_rwe_command(int uart_stream, uint32_t start_adress, uint32_t end_adress, command_types_t rwe_choice)
{
    read_write_erase_command_t rwe_command;
    response_t response;

    rwe_command.start_of_frame = START_OF_COMMAND_FRAME_PATTERN;
    rwe_command.length         = __bswap_16(PAYLOAD_LENGTH_WE_COMMAND);
    switch(rwe_choice)
    {
        case SAFETY_UPDATE_READ:
        {
            rwe_command.command        =  READ_COMMAND_PATTERN;
            break;
        }
        case SAFETY_UPDATE_WRITE:
        {
            rwe_command.command        =  WRITE_COMMAND_PATTERN;
            break;
        }
        case SAFETY_UPDATE_ERASE:
        {
            rwe_command.command        =  ERASE_COMMAND_PATTERN;
            break;
        }

        default:
        {
            fprintf(stderr, "safety_update_transmit_rwe_command called with rwe_choice =  %d\n", (int)rwe_choice);
            return -1;
        }
    }

    rwe_command.start_adress   = __bswap_32(start_adress);
    rwe_command.end_adress     = __bswap_32(end_adress);
    safety_update_add_checksum_to_rwe_command(&rwe_command);
    rwe_command.end_of_frame   = END_OF_FRAME_PATTERN;
    
    write(uart_stream, &rwe_command, sizeof(rwe_command));

    if(rwe_choice == SAFETY_UPDATE_READ)
    {
        // in the read process we are ready here .. we dont get a status packet back
        return 0;
    }
  
    // For write and rease: After some time there should be a response from the host
    int received_bytes = read_uart_buffer(uart_stream, g_rx_buffer, sizeof(response));

    if(received_bytes != sizeof(response))
    {
        fprintf(stderr, "Expected length of inquiry request: %d, vs received %d\n", sizeof(response), received_bytes);
        return -1;
    }

    memcpy(&response, g_rx_buffer, sizeof(response));

    if(safety_update_check_response(&response) == false)
    {
        fprintf(stderr, "Failed erase response\n");
        return -1;
    }

    if(rwe_choice == SAFETY_UPDATE_ERASE)
    {
        if((response.response != ERASE_COMMAND_PATTERN) || (response.status_code != STATUS_CODE_OK_PATTERN))
        {
            fprintf(stderr, "Error in erase response RES: 0x%2X, STS 0x%2X\n", response.response, response.status_code);
            return -1;
        }
    }
    else
    {
        if((response.response != WRITE_COMMAND_PATTERN) || (response.status_code != STATUS_CODE_OK_PATTERN))
        {
            fprintf(stderr, "Error in write response RES: 0x%2X, STS 0x%2X", response.response, response.status_code);
            return -1;
        }
    }

    return 0;
}


static ssize_t read_uart_buffer(int fd, uint8_t* buf, size_t buffer_size)
{
    ssize_t received_bytes;
    ssize_t accumulated_bytes;
    uint8_t test = 0xAA;

    received_bytes = read(fd, buf, buffer_size);
    accumulated_bytes = received_bytes;

    if(received_bytes == 0)
    {
        write(fd, &test, sizeof(test));
    }

    while( (received_bytes == UART_HW_BUFFER) && (buffer_size > UART_HW_BUFFER) )
    {
        buffer_size -= UART_HW_BUFFER;
        buf+= UART_HW_BUFFER;
        received_bytes = read(fd, buf, buffer_size);
        accumulated_bytes += received_bytes;

    }

    return accumulated_bytes;

}

static int safety_update_write_firmware(int uart_stream, const char *filename)
{
    int retValue;
    uint32_t start_adress = 0u;
    FILE *firmware = fopen (filename,"rb");
    if(firmware == NULL)
    {
        fprintf(stderr, "Not able to open file\n");
        return -1;
    }

    // 0% for process until now
    g_update_progress = 0u;

    while(safety_update_fill_data_packet(firmware))
    {
        retValue = safety_update_transmit_rwe_command(uart_stream, start_adress, (start_adress+MAXIMUM_NUMBER_OF_DATA_BYTES-1), SAFETY_UPDATE_WRITE);
        if(retValue != 0)
        {
            fprintf(stderr, "safety_update_write_command failed\n");
        }
        start_adress += MAXIMUM_NUMBER_OF_DATA_BYTES;
        retValue = safety_update_transmit_data_packet(uart_stream);
        if(retValue != 0)
        {
            fprintf(stderr, "safety_update_write_data_packet failed\n");
            return -1;
        }    

        // update the process rate
        if(g_stored_firware_info.application_size != 0)
        {
            g_update_progress =  (start_adress*100) / g_stored_firware_info.application_size;
        }
        
    }

    // send out the last packet
    retValue = safety_update_transmit_rwe_command(uart_stream, start_adress, (start_adress+MAXIMUM_NUMBER_OF_DATA_BYTES-1), SAFETY_UPDATE_WRITE);
    if(retValue != 0)
    {
        fprintf(stderr, "safety_update_write_command failed\n");
    }

    // update the process rate
    g_update_progress = 100u;

    retValue = safety_update_transmit_data_packet(uart_stream);
    if(retValue != 0)
    {
        fprintf(stderr, "safety_update_write_data_packet failed\n");
        return -1;
    }

    fclose(firmware);
    return 0;

}

static bool safety_update_fill_data_packet(FILE * file_pointer)
{
    size_t read_bytes;
    // Initialize with FF first
    memset(g_data_packet.data, 0xFF, MAXIMUM_NUMBER_OF_DATA_BYTES);
    read_bytes = fread(g_data_packet.data, 1, MAXIMUM_NUMBER_OF_DATA_BYTES, file_pointer);

    g_data_packet.start_of_frame = START_OF_DATA_FRAME_PATTERN;
    g_data_packet.length         = __bswap_16(PAYLOAD_LENGTH_DATA);
    g_data_packet.response       = WRITE_COMMAND_PATTERN;
    safety_update_add_checksum_to_data_packet();
    g_data_packet.end_of_frame   = END_OF_FRAME_PATTERN;

    // If we read less bytes than MAX we are ready
    if(read_bytes < MAXIMUM_NUMBER_OF_DATA_BYTES)
    {
        return false;
    }

    return true;
}

static int safety_update_transmit_data_packet(int uart_stream)
{
    int received_bytes = 0;
    response_t response;
    uint8_t test = 0xA5;
   
    // Write directly from our module local data packet
    write(uart_stream, &g_data_packet, sizeof(g_data_packet));

  
    // After some time there should be a response from the host
    received_bytes = read_uart_buffer(uart_stream, g_rx_buffer, sizeof(response));

    // First check the received length
    if(received_bytes != sizeof(response))
    {
        write(uart_stream, &test, sizeof(test));
        fprintf(stderr, "Expected length of data response: %d, vs received %d\n", sizeof(response), received_bytes);
        return -1;
    }

    // Now we are sure about the size and we can copy that in the structure
    memcpy(&response, g_rx_buffer, sizeof(response));

    // Check if the response itself has a failure
    if(safety_update_check_response(&response) == false)
    {
        fprintf(stderr, "Failed data response");
        return -1;
    }

    if((response.response != WRITE_COMMAND_PATTERN) || (response.status_code != STATUS_CODE_OK_PATTERN))
    {
        fprintf(stderr, "Error in data response RES: 0x%2X, STS 0x%2X", response.response, response.status_code);
        return -1;
    }

    return 0;
}

static void safety_update_add_checksum_to_data_packet()
{
    // 2 byte length + 1 byte command + 1024 byte data
    const uint16_t sum_size = 3u + MAXIMUM_NUMBER_OF_DATA_BYTES;
    uint8_t sum = 0x00;
    uint8_t* pointer_data;

    // We map that to a uint8 pointer
    pointer_data = (uint8_t*)&g_data_packet;

    // First byte is the start byte ... this needs to be skipped
    pointer_data ++;

    // Add sum over the next 1024 + 3 bytes
    for(uint16_t i = 0; i< sum_size; i++)
    {
        // Overflow is intended here ... this is like a CRC check
        sum += (*pointer_data);
        pointer_data ++;
    }

    // sum_data is the CRC check ... the sum of all data plus sum_data shall result in zero
    // This means we have to calculate the 2 complement of sum and transmit that in the variable sum_data
    g_data_packet.sum_data = (uint8_t)  (0x00 - sum);
}