#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libserialport.h>

char *serial_port = "/dev/ttyACM0";

typedef struct sp_port sp_port_t;
typedef struct sp_port_config sp_port_config_t;
typedef enum sp_return sp_return_t;

sp_port_t *port;

uint32_t program_addr;
#define BUFSIZE 128
char input_buf[BUFSIZE];
char read_buf[BUFSIZE];

/* Read a line of text up to the first CR (or timeout) */
int get_response(int timeout)
{
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
    printf("<-- [%s]\n", input_buf);
    return 1;
}

// Wait for ? prompt
int handshake()
{
    //sp_nonblocking_write(port, "##", 2);
    //sp_drain(port);

    get_response(500);
    
    if (strcmp(input_buf, "?") == 0) {
        return 1;
    }
    return 0;
}

void show_debug()
{
    while (1)
    {
        char buf[16];
        int result = sp_blocking_read(port, buf, 1, 500);
        if (result == 0)
        {
            break;
        }
        printf("%c", buf[0]);
    }
    printf("\n");
}

void wait_for_response()
{
    if (!get_response(200))
    {
        fprintf(stderr, "No response from device.\n");
        show_debug();
        exit(5);
    } 

    if (input_buf[0] != '#')
    {
        fprintf(stderr, "Unexpected response: %s\n", input_buf);
        show_debug();
        exit(5);
    }
}

void send_command(const char *command)
{
    printf("--> %s\n", command);
    sp_nonblocking_write(port, command, strlen(command));
    sp_nonblocking_write(port, "\n", 1);
    sp_drain(port);
    wait_for_response();
    // printf("<-- %s\n", input_buf);
}

int do_parameters(int argc, char **argv)
{
    int c;
    
    // Defaults
    program_addr = 0;
        
    while ((c = getopt(argc, argv, "a:s:")) != -1)
    {
        switch (c)
        {
            case 'a':
                program_addr = atoi(optarg);
                break;
            case 's':
                serial_port = optarg;
                break;
        }
    }
    
    return optind;
}

// TODO: finish
void do_read(uint8_t *read_buf, uint32_t read_addr, uint32_t size) {
    /* Send read command */
    char buf[32];
    sprintf(buf, "r %d %d", read_addr, size);
    send_command(buf);

    /* Display response */
    int expected_addr = read_addr;
    do {
        get_response(500);

        char *next = input_buf;
        int addr = strtol(input_buf, &next, 16);
        if (*next == ':') {
            next++;
        }
        int value = strtol(next, 0, 16);

        if (addr != expected_addr) {
            fprintf(stderr, "Unexpected data out of order at address 0x%x.\n", expected_addr);
            exit(1);
        }
        //printf("read addr: %d, val %04x\n", addr, value);
        expected_addr++;
    } while (expected_addr < read_addr + size);
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


int main(int argc, char **argv)
{
    int lastopt = do_parameters(argc, argv);

    if (argc == lastopt)
    {
        fprintf(stderr, "\nusage: %s [options] imagefilename\n", argv[0]);
        fprintf(stderr, "\n\t-a address:\tAddress in EPROM device to begin programming\n");
        fprintf(stderr, "\n\t-s serialdev:\tSerial port device\n");
        fprintf(stderr, "\n");
        
        exit(1); 
    }

    const char *filename = argv[lastopt];
    FILE *infile = fopen(filename, "rb");
    if (infile == 0)
    {
        fprintf(stderr, "Couldn't load %s.\n", filename);
        exit(1);
    }

    //const uint32_t filelen = 4 / 2; // in words

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
    
    // Wait for handshake
    printf("# Attempting handshake...\n");

    // You have to wait an age for Arduinos to wake up
    usleep(2000000);

    if (!handshake())
    {
        fprintf(stderr, "Didn't receive handshake.\n");
        exit(4);
    }

    printf("# Got handshake.\n");

    uint8_t file_buf[1024];
    size_t read_result = fread(file_buf, 4, 1, infile);
    if (read_result != 1) {
        fprintf(stderr, "Couldn't read input file!\n");
        exit(1);
    }

    do_buffer(file_buf, 2);
    do_program(10, 2);

    do_read(file_buf, 0, 16);

    fclose(infile);
	sp_close(port);
    sp_free_config(conf);
	sp_free_port(port);

	return 0;
}
