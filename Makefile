CC = gcc
TIMESTAMP := $(shell TZ="America/New_York" date +"%Y-%m-%d %H:%M EST")
MD_FILES := README.md CONTRIBUTING.md ROADMAP.md

CFLAGS = -Wall -Wextra -O2 -Isrc/proto -Isrc/api -Iinclude -Isrc -I/usr/local/include -If:/AI/Projects/esp_c_api/noise-c/include

# Standard system paths for the installed library
LDFLAGS = -Lf:/AI/Projects/esp_c_api/noise-c/src/protocol -L/usr/local/lib -Wl,-rpath,/usr/local/lib

# Link MbedTLS crypto (mbedcrypto), noise protocol (noiseprotocol), and winsock
LIBS = -lnoiseprotocol -lmbedcrypto -lws2_32

# NanoPB Generator Config (Adjust path to your nanopb installation)
NANOPB_GEN = C:/Users/Owner/AppData/Local/Programs/Python/Python312/Scripts/nanopb_generator.exe

# NanoPB and API Source Files
PROTO_DIR = src/proto
API_DIR = src/api
SRC_DIR = src

# Protocol definition files
PROTO_FILES = $(PROTO_DIR)/api.proto \
              $(PROTO_DIR)/api_options.proto

# Generated files
PROTO_SRCS = $(PROTO_FILES:.proto=.pb.c)
PROTO_HDRS = $(PROTO_FILES:.proto=.pb.h)

# Nanopb core implementation (assumed to be in PROTO_DIR)
NANOPB_CORE = $(PROTO_DIR)/pb_common.c \
              $(PROTO_DIR)/pb_encode.c \
              $(PROTO_DIR)/pb_decode.c

PROTO_OBJS = $(PROTO_SRCS:.c=.o) $(NANOPB_CORE:.c=.o)

API_SRCS = $(API_DIR)/esphome_api.c \
           $(API_DIR)/entity_registry.c \
           $(API_DIR)/frame.c \
           $(API_DIR)/proto_helpers.c \
           $(API_DIR)/esphome_api.pb.c

API_OBJS = $(API_SRCS:.c=.o)

NOISE_SRCS = src/noise/noise.c
NOISE_OBJS = $(NOISE_SRCS:.c=.o)

TRANSPORT_SRCS = src/transport/transport.c
TRANSPORT_OBJS = $(TRANSPORT_SRCS:.c=.o)

CLIENT_MAIN_SRCS = examples/linux_client.c
CLIENT_MAIN_OBJS = $(CLIENT_MAIN_SRCS:.c=.o)

TARGET = real_esphome_client

ALL_OBJS = $(CLIENT_MAIN_OBJS) $(API_OBJS) $(NOISE_OBJS) $(TRANSPORT_OBJS) $(PROTO_DIR)/pb_common.o $(PROTO_DIR)/pb_encode.o $(PROTO_DIR)/pb_decode.o

all: $(TARGET)

$(TARGET): $(ALL_OBJS)
	$(CC) $(CFLAGS) $(ALL_OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

# Rule to generate Nanopb files from .proto files
protos: $(PROTO_SRCS) $(PROTO_HDRS) # Ensure both .c and .h are targets for generation

$(PROTO_DIR)/%.proto:
	$(error Protocol definition file $@ is missing. Please ensure the ESPHome .proto files are placed in $(PROTO_DIR))

$(PROTO_DIR)/%.pb.c $(PROTO_DIR)/%.pb.h: $(PROTO_DIR)/%.proto
	@mkdir -p $(PROTO_DIR)
	@echo "Generating Nanopb files from $<..."
	@$(NANOPB_GEN) --output-dir=$(PROTO_DIR) -I $(PROTO_DIR) $< || \
		(echo "Error: nanopb_generator failed. Ensure 'nanopb' is installed in your pythonenv: pip install nanopb"; exit 1)

update_metadata:
	@echo "Updating timestamps to $(TIMESTAMP)..."
	@sed -i 's/\*Last Updated: .* .*\*/\*Last Updated: $(TIMESTAMP)\*/' $(MD_FILES)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o src/*.o src/api/*.o src/noise/*.o src/transport/*.o examples/*.o $(TARGET)

run_client:
	./$(TARGET) $(IP) $(PSK) $(PORT)

.PHONY: all clean update_metadata protos run_client