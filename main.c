#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libserialport.h>

#include "crc16.h"

// Serial-port-related globals
char *serial_port = "/dev/ttyACM0";
typedef struct sp_port sp_port_t;
typedef struct sp_port_config sp_port_config_t;
typedef enum sp_return sp_return_t;
sp_port_t *port;

// Functionality-related globals
uint32_t program_addr;
uint32_t program_end_addr;
uint32_t program_size;
#define BUFSIZE 128
char input_buf[BUFSIZE];
char read_buf[BUFSIZE];
int read_flag;

//#define DEBUG

/* Read a line of text up to the first CR (or timeout) */
int get_response(int timeout) {
    sp_return_t result;
    int pos = 0;
    char c;
    do {
        // Read one character
        result = sp_blocking_read(port, &c, 1, timeout);
        if (result == 0)
        {
            input_buf[pos] = '\0';
            return 0;
        }

        if (pos < BUFSIZE - 1 && c != '\n' && c != '\r') {
            input_buf[pos++] = c;
        }

    } while (c != '\n');

    input_buf[pos] = '\0';
#ifdef DEBUG
    printf("<-- [%s]\n", input_buf);
#endif
    return 1;
}

// Wait for ? prompt
int get_prompt() {
    get_response(500);
    
    if (strcmp(input_buf, "?") == 0) {
        return 1;
    }
    return 0;
}

void wait_for_response() {
    if (!get_response(2000)) {
        fprintf(stderr, "No response from device.\n");
        exit(5);
    } 

    if (input_buf[0] != '#') {
        fprintf(stderr, "Unexpected response: %s\n", input_buf);
        exit(5);
    }
}

void send_command(const char *command) {
#ifdef DEBUG
    printf("--> %s\n", command);
#endif
    sp_nonblocking_write(port, command, strlen(command));
    sp_nonblocking_write(port, "\n", 1);
    sp_drain(port);
    wait_for_response();
#ifdef DEBUG
    printf("<-- %s\n", input_buf);
#endif
}

int do_parameters(int argc, char **argv) {
    int c;
    
    // Defaults
    program_addr = 0;
    program_end_addr = -1;
    program_size = -1;
    read_flag = 0;
        
    while ((c = getopt(argc, argv, "a:s:z:e:r")) != -1)
    {
        switch (c)
        {
            case 'a':
                program_addr = atoi(optarg);
                break;
            case 'e':
                program_end_addr = atoi(optarg);
                break;
            case 'r':
                read_flag = 1;
                break;
            case 's':
                serial_port = optarg;
                break;
            case 'z':
                program_size = atoi(optarg);
                break;
        }
    }
    
    return optind;
}

void do_read(uint8_t *read_buf, uint32_t read_addr, uint32_t size) {
    /* Send read command */
    char buf[32];
    sprintf(buf, "r %d %d", read_addr, size);
    send_command(buf);

    /* Store response */
    int expected_addr = read_addr;
    uint32_t byte = 0;
    do {
        get_response(500);

        char *next;
        int addr = strtol(input_buf, &next, 16);
        while (*next == ':') {
            next++;
        }
        while (*next == ' ') {
            next++;
        }
        int value = strtol(next, 0, 16);

        if (addr != expected_addr) {
            fprintf(stderr, "Unexpected data out of order at address 0x%x.\n", expected_addr);
            exit(1);
        }
        //printf("read addr: %d, val %04x\n", addr, value);

        read_buf[byte++] = value >> 8;
        read_buf[byte++] = value & 0xff;
        expected_addr++;
    } while (expected_addr < read_addr + size);

    /* Display response */
    get_response(500);

    // Wait for READY
    if (strcmp(input_buf, "?") != 0) {
        fprintf(stderr, "Unexpected response: [%s]\n", input_buf);
        exit(1);      
    }    
}    

// Size is in words
void do_buffer(uint8_t *send_buf, uint32_t size) {
    /* Send buffer command */
    char buf[32];
    sprintf(buf, "b %d", size);
    send_command((const char *)buf);

    /* Display response */
    get_response(500);

    // Wait for READY
    if (strcmp(input_buf, "READY") != 0) {
        fprintf(stderr, "Unexpected response: [%s]\n", input_buf);
        exit(1);      
    }

    // Send data
    sp_nonblocking_write(port, send_buf, size * 2);

    get_response(500);
    if (strncmp(input_buf, "OK ", 2) != 0) {
        fprintf(stderr, "Unexpected response: [%s]\n", input_buf);
        exit(1);      
    }

    int total = strtol(input_buf + 3, 0, 10);
    if (total != size) {
        fprintf(stderr, "Unexpected size response: %d\n", total);
        exit(1);      
    }

    get_response(500);
}    

// Program from buffer to EPROM, size is in words
void do_program(uint32_t addr, uint32_t size) {
    /* Send buffer command */
    char buf[32];
    sprintf(buf, "p %d %d", addr, size);
    send_command((const char *)buf);

    /* Display response */
    get_response(500);

    // Wait for READY
    if (strcmp(input_buf, "?") != 0) {
        fprintf(stderr, "Unexpected response: [%s]\n", input_buf);
        exit(1);      
    }
}

// Program from buffer to EPROM, size is in words
uint16_t do_crc(uint32_t addr, uint32_t size) {
    /* Send buffer command */
    char buf[32];
    sprintf(buf, "c %d %d", addr, size);
    send_command((const char *)buf);

    if (strncmp(input_buf, "# CRC ", 6) != 0) {
        fprintf(stderr, "Unexpected response: [%s]\n", input_buf);
        exit(1);      
    }
    char *next = input_buf + 6;
    uint32_t response_addr = strtol(next, &next, 10);
    while (*next == ' ') {
        next++;
    }
    uint32_t response_size = strtol(next, &next, 10);
    while (*next == ' ') {
        next++;
    }
    uint32_t response_crc = strtol(next, 0, 16);

    if (response_addr != addr || response_size != size) {
        fprintf(stderr, "CRC command returned incorrect result: [%s]\n", input_buf);
        exit(1);      
    }

    get_response(200);   
   
    if (strcmp(input_buf, "?") != 0) {
        fprintf(stderr, "Unexpected response: [%s]\n", input_buf);
        exit(1);      
    }
    return response_crc;
}

uint16_t calc_crc16(uint8_t *block, uint32_t size) {
  uint16_t crc = 0;
  for (uint32_t addr = 0; addr < size; addr++) {
    crc = crc16(crc, block[addr]);
  }
  return crc;
}

void write_from_file(const char *filename) {
    FILE *infile = fopen(filename, "rb");
    if (infile == 0)
    {
        fprintf(stderr, "Couldn't load %s.\n", filename);
        exit(1);
    }

    // Find file size
    fseek(infile, 0, SEEK_END);    
    size_t infile_size = ftell(infile);
    rewind(infile);

    printf("ROM file is %lu bytes.\n", infile_size);
    printf("Programming %lu bytes at ROM address 0x%06x.\n", infile_size, program_addr);

    uint8_t file_buf[1024];
    uint32_t end_addr = program_addr + infile_size;
    uint32_t block_addr = program_addr;
    while (block_addr < end_addr) {
        uint32_t block_size = end_addr - block_addr > 512 ? 512 : end_addr - block_addr;
        uint32_t num_words = block_size / 2;
        uint32_t addr_words = block_addr / 2;
        
        printf("Programming block of size %d at ROM address 0x%06x...\n", block_size, block_addr);

        // Get the next block of data from disk
        size_t read_result = fread(file_buf, block_size, 1, infile);
        if (read_result != 1) {
            fprintf(stderr, "Couldn't read input file!\n");
            exit(1);
        }

        uint16_t block_crc = calc_crc16(file_buf, block_size);

        do_buffer(file_buf, num_words);
        do_program(addr_words, num_words);
        uint16_t crc = do_crc(addr_words, num_words);
        if (crc != block_crc) {
            fprintf(stderr, "Mismatching CRC16: %04x (EPROM) vs %04x (file)\n", crc, block_crc);
            exit(1);
        }

        block_addr += block_size;
    }

    printf("Write complete.\n");
    fclose(infile);
}

void read_to_file(const char *filename) {
    FILE *outfile = fopen(filename, "wb");
    if (outfile == 0)
    {
        fprintf(stderr, "Couldn't open file %s for writing.\n", filename);
        exit(1);
    }

    uint8_t file_buf[1024];
    uint32_t end_addr;
    
    if (program_end_addr == -1 && program_size == -1) {
        fprintf(stderr, "No size (-e or -z) specified. Can't continue.\n");
        exit(1);
    } else if (program_end_addr != -1) {
        end_addr = program_end_addr;
    } else {
        end_addr = program_addr + program_size;
    }
    
    uint32_t block_addr = program_addr;
    while (block_addr < end_addr) {
        uint32_t block_size = end_addr - block_addr > 512 ? 512 : end_addr - block_addr;
        uint32_t num_words = block_size / 2;
        uint32_t addr_words = block_addr / 2;
        
        printf("Reading block of size %d at ROM address 0x%06x...\n", block_size, block_addr);

        do_read(file_buf, addr_words, num_words);

        // Write the block of data to disk
        size_t write_result = fwrite(file_buf, block_size, 1, outfile);
        if (write_result != 1) {
            fprintf(stderr, "Couldn't write output file!\n");
            exit(1);
        }

        block_addr += block_size;
    }

    printf("Read complete.\n");
    fclose(outfile);
}

int main(int argc, char **argv)
{
    int lastopt = do_parameters(argc, argv);

    if (argc == lastopt)
    {
        fprintf(stderr, "\nusage: %s [options] imagefilename\n", argv[0]);
        fprintf(stderr, "\n\t-a address:\tAddress in EPROM device to begin programming\n");
        fprintf(stderr, "\n\t-s serialdev:\tSerial port device\n");
        fprintf(stderr, "\n\t-r Read from EPROM to file instead of the other way around\n");
        fprintf(stderr, "\n\t-e Set end address\n");
        fprintf(stderr, "\n\t-z Set size of program zone\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "When reading, one of -s or -z is required.\n");
        
        exit(1); 
    }

	sp_return_t result = sp_get_port_by_name(serial_port, &port);
	if (result != SP_OK)
	{
		fprintf(stderr, "Couldn't open %s (1)\n", serial_port);
		exit(1);
	}

	result = sp_open(port, SP_MODE_READ | SP_MODE_WRITE);

	if (result != SP_OK)
	{
		fprintf(stderr, "Couldn't open %s (2)\n", serial_port);
		exit(2);
	}

    // Set up port parameters
    sp_port_config_t *conf;
    result = sp_new_config(&conf);
    result = result == SP_OK ? sp_set_config_baudrate(conf, 19200) : result;
    result = result == SP_OK ? sp_set_config_parity(conf, SP_PARITY_NONE) : result;
    result = result == SP_OK ? sp_set_config_bits(conf, 8) : result;
    result = result == SP_OK ? sp_set_config_stopbits(conf, 1) : result;
    result = result == SP_OK ? sp_set_config_flowcontrol(conf, SP_FLOWCONTROL_NONE) : result;
    result = result == SP_OK ? sp_set_config(port, conf) : result;

	if (result != SP_OK)
	{
		fprintf(stderr, "Couldn't configure port\n");
		exit(3);
	}

    // wait for silence
    while (1)
    {
        sp_return_t result = sp_blocking_read(port, input_buf, 1, 250);
        if (result == 0)
            break;
        printf("Received %c\n", input_buf[0]);
    }
    
    // Wait for prompt
    printf("# Waiting for device...\n");

    // You have to wait an age for Arduinos to wake up
    usleep(2000000);

    if (!get_prompt())
    {
        fprintf(stderr, "Didn't see prompt.\n");
        exit(4);
    }

    printf("# Got prompt.\n");

    const char *filename = argv[lastopt];

    if (read_flag == 0) {
        write_from_file(filename);
    } else {
        read_to_file(filename);
    }

    /*
*/

	sp_close(port);
    sp_free_config(conf);
	sp_free_port(port);

	return 0;
}
